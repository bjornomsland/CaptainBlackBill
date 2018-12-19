#include "cptblackbill.hpp"
//#include "eosio.token.hpp"

using namespace eosio;

class [[eosio::contract]] cptblackbill : public eosio::contract {

public:
    using contract::contract;
      
    cptblackbill(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}
    
    //Create token
    [[eosio::action]]
    void create(name issuer, asset maximum_supply )
    {
        require_auth( _self );

        auto sym = maximum_supply.symbol;
        eosio_assert( sym.is_valid(), "invalid symbol name" );
        eosio_assert( maximum_supply.is_valid(), "invalid supply");
        eosio_assert( maximum_supply.amount > 0, "max-supply must be positive");

        stats statstable( _self, sym.code().raw() );
        auto existing = statstable.find( sym.code().raw() );
        eosio_assert( existing == statstable.end(), "token with symbol already exists" );

        statstable.emplace( _self, [&]( auto& s ) {
            s.supply.symbol = maximum_supply.symbol;
            s.max_supply    = maximum_supply;
            s.issuer        = issuer;
        });
    }

    //Issue token
    [[eosio::action]]
    void issue(name to, asset quantity, std::string memo )
    {
        auto sym = quantity.symbol;
        eosio_assert( sym.is_valid(), "invalid symbol name" );
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

        stats statstable( _self, sym.code().raw() );
        auto existing = statstable.find( sym.code().raw() );
        eosio_assert( existing != statstable.end(), "token with symbol does not exist, create token before issue" );
        const auto& st = *existing;

        require_auth( st.issuer );
        eosio_assert( quantity.is_valid(), "invalid quantity" );
        eosio_assert( quantity.amount > 0, "must issue positive quantity" );

        eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
        eosio_assert( quantity.amount <= st.max_supply.amount - st.supply.amount, "quantity exceeds available supply");

        statstable.modify( st, same_payer, [&]( auto& s ) {
            s.supply += quantity;
        });

        add_balance( st.issuer, quantity, st.issuer );

        if( to != st.issuer ) {
            SEND_INLINE_ACTION( *this, transfer, { {st.issuer, "active"_n} }, { st.issuer, to, quantity, memo });
        }
    }

    //Retire token
    [[eosio::action]]
    void retire( asset quantity, std::string memo )
    {
        auto sym = quantity.symbol;
        eosio_assert( sym.is_valid(), "invalid symbol name" );
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

        stats statstable( _self, sym.code().raw() );
        auto existing = statstable.find( sym.code().raw() );
        eosio_assert( existing != statstable.end(), "token with symbol does not exist" );
        const auto& st = *existing;

        require_auth( st.issuer );
        eosio_assert( quantity.is_valid(), "invalid quantity" );
        eosio_assert( quantity.amount > 0, "must retire positive quantity" );

        eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );

        statstable.modify( st, same_payer, [&]( auto& s ) {
        s.supply -= quantity;
        });

        sub_balance( st.issuer, quantity );
    }

    //Transfer token
    [[eosio::action]]
    void transfer(name from, name to, asset quantity, std::string memo )
    {
        eosio_assert( from != to, "cannot transfer to self" );
        require_auth( from );
        eosio_assert( is_account( to ), "to account does not exist");
        auto sym = quantity.symbol.code();
        stats statstable( _self, sym.raw() );
        const auto& st = statstable.get( sym.raw() );

        require_recipient( from );
        require_recipient( to );

        eosio_assert( quantity.is_valid(), "invalid quantity" );
        eosio_assert( quantity.amount > 0, "must transfer positive quantity" );
        eosio_assert( quantity.symbol == st.supply.symbol, "symbol precision mismatch" );
        eosio_assert( memo.size() <= 256, "memo has more than 256 bytes" );

        auto payer = has_auth( to ) ? to : from;

        sub_balance( from, quantity );
        add_balance( to, quantity, payer );
    }

    void sub_balance(name owner, asset value) 
    {
        accounts from_acnts( _self, owner.value );

        const auto& from = from_acnts.get( value.symbol.code().raw(), "no balance object found" );
        eosio_assert( from.balance.amount >= value.amount, "overdrawn balance" );

        from_acnts.modify( from, owner, [&]( auto& a ) {
            a.balance -= value;
        });
    }

    void add_balance( name owner, asset value, name ram_payer )
    {
        accounts to_acnts( _self, owner.value );
        auto to = to_acnts.find( value.symbol.code().raw() );
        if( to == to_acnts.end() ) {
            to_acnts.emplace( ram_payer, [&]( auto& a ){
                a.balance = value;
            });
        } else {
            to_acnts.modify( to, same_payer, [&]( auto& a ) {
                a.balance += value;
            });
        }
    }

    //===Receive EOS token=================================================
    void onTransfer(name from, name to, asset eos, std::string memo) { 
        // verify that this is an incoming transfer
        if (to != name{"cptblackbill"})
            return;

        eosio_assert(eos.symbol == symbol(symbol_code("EOS"), 4), "must pay with EOS token");
        eosio_assert(eos.amount > 0, "deposit amount must be positive");

        size_t pos = memo.find("RCVT");
        std::string strtreasurepkey = memo.substr(pos + 4);
        uint64_t treasurepkey = std::strtoull(strtreasurepkey.c_str(),NULL,0);
        
        treasure_index treasures(_self, _self.value);
        auto iterator = treasures.find(treasurepkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found.");

        //Tag the transfered amount on the treasure so the modtrchest-function can store the treasure value as an encrypted(hidden value)
        treasures.modify(iterator, _self, [&]( auto& row ) {
            row.prechesttransfer += eos;
        });
    }
    //=====================================================================

    [[eosio::action]]
    void addtreasure(name user, std::string title, std::string imageurl, double latitude, double longitude, std::string treasurechestsecret) {
        require_auth(user);
        
        eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
        eosio_assert(imageurl.length() <= 100, "Max length of imageUrl is 100 characters.");

        bool locationIsValid = true;
        if((latitude < -90 || latitude > 90) || latitude == 0) {
            locationIsValid = false;
        }

        if((longitude < -180 || longitude > 180) || longitude == 0){
            locationIsValid = false;
        }
        
        eosio_assert(locationIsValid, "Location (latitude and/ord longitude) is not valid.");
        
        treasure_index treasures(_code, _code.value);
        
        treasures.emplace(user, [&]( auto& row ) {
            row.pkey = treasures.available_primary_key();
            row.owner = user;
            row.title = title;
            row.imageurl = imageurl;
            row.latitude = latitude;
            row.longitude = longitude;
            row.treasurechestsecret = treasurechestsecret;
            row.totalturnover = eosio::asset(0, symbol(symbol_code("EOS"), 4)); 
            row.sellingprice = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.timestamp = now();
        });
    }

    [[eosio::action]]
    void modtreasure(name user, uint64_t pkey, std::string title, std::string description, std::string imageurl, std::string videourl,
                    std::string category, int32_t level) {
            require_auth( user );
            treasure_index treasures(_code, _code.value);
            auto iterator = treasures.find(pkey);
            eosio_assert(iterator != treasures.end(), "Treasure not found");
            eosio_assert(iterator->owner == user, "You don't have access to modify this treasure.");

            eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
            eosio_assert(description.length() <= 650, "Max length of description is 650 characters.");
            //eosio_assert(imageUrl.find("https") == 0, "Invalid image URL. Must be from a secure server and start with lower case 'https'");
            eosio_assert(imageurl.length() <= 100, "Max length of image url is 100 characters.");
            //eosio_assert(videoUrl.find("https") == 0, "Invalid video URL. Must be from a secure server and start with lower case 'https'");
            eosio_assert(videourl.length() <= 100, "Max length of video url is 100 characters.");
            eosio_assert(category.length() <= 50, "Max length of category is 50 characters.");

            treasures.modify(iterator, user, [&]( auto& row ) {
                row.title = title;
                row.description = description;
                row.imageurl = imageurl;
                row.videourl = videourl;
                row.category = category;
                row.level = level;
            });
    }

    /*[[eosio::action]]
    void checktreasur(name user, uint64_t pkey, asset quantity) {
        require_auth(user);
        
        treasure_index treasures(_code, _code.value);
        
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure does not exist.");
        
        //INLINE_ACTION_SENDER(eosio::token, transfer)( N(eosio.token), {user,N(active)},{ user, N(eosio), quantity, std::string("buy ram") } );

        //eosio_assert(iterator->owner == user, "You don't have access to remove this treasure.");
        //treasures.erase(iterator);
    }*/

    [[eosio::action]]
    void modtrchest(name user, uint64_t pkey, std::string treasurechestsecret, int32_t videoviews, asset totalturnover) {
        require_auth(user);
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        
        name oracleaccount = name{"cptblackbill"};
        eosio_assert(user == oracleaccount, "Updating trcf is only allowed by CptBlackBill.");

        uint64_t rankingpoints = (videoviews * 1000) + totalturnover.amount;
        eosio::asset currentprechesttransfer = iterator->prechesttransfer; 

        treasures.modify(iterator, user, [&]( auto& row ) {
            row.treasurechestsecret = treasurechestsecret; //This is the encrypted value of the treasure. It's not very hard to decrypt if someone finds that more exciting than reading the code on location. But for most of us it's easier to just pay $2 to get the treasure value.
            row.videoviews = videoviews;
            row.totalturnover = totalturnover;
            row.rankingpoints = rankingpoints;
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4)); //Clear this - amount has been encrypted and stored in the treasurechestsecret
        });
        
        eosio::asset fivepercenttotokenholders = (currentprechesttransfer * (5 * 100)) / 10000;
        action(
            permission_level{ get_self(), "active"_n },
            "eosio.token"_n, "transfer"_n,
            std::make_tuple(get_self(), "testnetbill4"_n, fivepercenttotokenholders, std::string("five percent to token holders"))
        ).send();

        //std::string message = "Successful transfered five percent cut to token holders fund account";
        //action(
        //    permission_level{get_self(),"active"_n},
        //    get_self(),
        //    "notify"_n,
        //    std::make_tuple(user, name{user}.to_string() + message)
        //).send();
    }

    [[eosio::action]]
    void erasetreasur(name user, uint64_t pkey) {
        require_auth(user);
        
        treasure_index treasures(_code, _code.value);
        
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure does not exist.");
        eosio_assert(iterator->owner == user, "You don't have access to remove this treasure.");

        treasures.erase(iterator);
    }

    [[eosio::action]]
    void addaward(name user, std::string title, std::string imageurl, std::string adlinkurl, asset awardvalue) {
        require_auth(user);
        
        eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
        eosio_assert(imageurl.length() <= 100, "Max length of imageUrl is 100 characters.");

        award_index awards(_code, _code.value);
        
        awards.emplace(user, [&]( auto& row ) {
            row.pkey = awards.available_primary_key();
            row.owner = user;
            row.title = title;
            row.imageurl = imageurl;
            row.adlinkurl = adlinkurl;
            row.awardvalue = awardvalue; //Award value in EOS.
        });
    }

    //Link award to treasure. The first person who solves the treasure will get this award (or the awards value) in addition to the treasure token value
    [[eosio::action]]
    void linkaward(name user, uint64_t award_pkey, uint64_t treasure_pkey) {
        require_auth(user);
        
        award_index awards(_code, _code.value);
        auto iteratoraward = awards.find(award_pkey);
        eosio_assert(iteratoraward != awards.end(), "Award not found");
        
        treasure_index treasures(_code, _code.value);
        auto iteratortrasure = treasures.find(treasure_pkey);
        eosio_assert(iteratortrasure != treasures.end(), "Treasure not found");

        //treasures.modify(iteratortrasure, user, [&]( auto& row ) {
        //    row.treasureawardid = award_pkey;
        //});
    }

    [[eosio::action]]
    void eraseaward(name user, uint64_t pkey) {
        require_auth(user);
        award_index awards(_code, _code.value);
        auto iterator = awards.find(pkey);
        eosio_assert(iterator != awards.end(), "Record does not exist");
        awards.erase(iterator);
    }

private:
    struct [[eosio::table]] account {
        asset    balance;
        uint64_t primary_key()const { return balance.symbol.code().raw(); }
    };

    struct [[eosio::table]] currency_stats {
        asset    supply;
        asset    max_supply;
        name     issuer;
        uint64_t primary_key()const { return supply.symbol.code().raw(); }
    };

    typedef eosio::multi_index< "accounts"_n, account > accounts;
    typedef eosio::multi_index< "stat"_n, currency_stats > stats;

    struct [[eosio::table]] treasure {
        uint64_t pkey;
        name owner;
        std::string title; 
        std::string description;
        std::string imageurl;
        std::string videourl; //Link to video (Must be a video provider that support API to views and likes)
        std::string category; //Climbing, biking, hiking, cross-country-skiing, etc
        double latitude; //GPS coordinate
        double longitude; //GPS coordinate
        int32_t level; //Difficulty Rating. Value 1-10 where 1 is very easy and 10 is very hard.
        int32_t videoviews; //Updated from Oracle 
        eosio::asset totalturnover; //Total historical value in BLCKBLs that has been paid out to users from the TC was made public. Updated from Oracle
        eosio::asset sellingprice; //Price if owner want to sell this treasure location to other user
        eosio::asset prechesttransfer; //Used when someone pay for checking treasure value. Token value is stored here until cptblackbill add tokens to encrypted treasure value
        uint64_t rankingpoints; //Calculated and updated from Oracle based on video and turnover stats.  
        int32_t timestamp; //Date created
        int32_t expirationdate; //Date when ownership expires - other users can then take ownnership of this treasure location
        std::string treasurechestsecret;
        std::string jsondata;  //additional field for other info in json format.
        std::string sponsorimageurl; //Advertise image from sponsor. User who solves treasure will get this product/award 
        name sponsorowneraccount;
        std::string sponsororderpageurl; //sponsor image will link to this web-page. Users must be able to buy and winners must be able to claim award from this page.
        eosio::asset sponsorawardvalue; //the value of the award that will be transfered to the first treasure finder
        eosio::asset sponsorfeetotreasurecreator; //the same value as the award, but this will be transfered to the owner of the treasure (equal payout for treasure owner and finder)

        //uint64_t primary_key() const { return key.value; }
        uint64_t primary_key() const { return  pkey; }
    };
    typedef eosio::multi_index<"treasure"_n, treasure> treasure_index;

    struct [[eosio::table]] award {
        uint64_t pkey;
        name owner;
        std::string title; 
        std::string description;
        std::string adlinkurl;
        std::string imageurl;
        eosio::asset awardvalue; //Value of the treasure award in EOS
        std::string jsondata;  //additional field for other info in json format.
        uint64_t primary_key() const { return  pkey; }
    };
    typedef eosio::multi_index<"award"_n, award> award_index;

    static constexpr uint64_t string_to_symbol( uint8_t precision, const char* str ) {
        uint32_t len = 0;
        while( str[len] ) ++len;

        uint64_t result = 0;
        for( uint32_t i = 0; i < len; ++i ) {
            if( str[i] < 'A' || str[i] > 'Z' ) {
                /// ERRORS?
            } else {
                result |= (uint64_t(str[i]) << (8*(1+i)));
            }
        }

        result |= uint64_t(precision);
        return result;
    }

};

//EOSIO_DISPATCH( cptblackbill, (create)(issue)(transfer)(addtreasure)(erasetreasur)(modtreasure)(checktreasur)(modtrchest))

extern "C" {
  void apply(uint64_t receiver, uint64_t code, uint64_t action) {
    auto self = receiver;
    //cptblackbill _cptblackbill(receiver);
    if(code==receiver && action==name("addtreasure").value) {
      execute_action(name(receiver), name(code), &cptblackbill::addtreasure );
    }
    else if(code==receiver && action==name("modtreasure").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modtreasure );
    }
    else if(code==receiver && action==name("modtrchest").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modtrchest );
    }
    else if(code==receiver && action==name("erasetreasur").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasetreasur );
    }
    else if(code==receiver && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::transfer );
    }
    else if(code==name("eosio.token").value && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::onTransfer );
    }
  }
};

