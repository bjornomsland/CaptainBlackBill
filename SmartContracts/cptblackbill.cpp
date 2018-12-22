#include "cptblackbill.hpp"
//#include "eosio.token.hpp"

using namespace eosio;

class [[eosio::contract]] cptblackbill : public eosio::contract {

public:
    using contract::contract;
      
    //_awardqutable(receiver, code.value)

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

        if (memo.rfind("Activate Sponsor Award No.", 0) == 0) {
            //from account pays for activating a sponsor award
            uint64_t sponsorqueuepkey = std::strtoull( memo.substr(26).c_str(),NULL,0 );

            sponsorqueue_index sponsorqueues(_self, _self.value);
            auto iterator = sponsorqueues.find(sponsorqueuepkey);
            eosio_assert(iterator != sponsorqueues.end(), "Sponsor award not found.");
            eosio_assert(eos == (iterator->sponsorawardvaluex2 + iterator->sponsorawardfee), "Paid amount don't match total cost of sponsor award.");
            
            //Tag sponsor award as paid and link to treasure if treasure don't have an active sponsor award already
            sponsorqueues.modify(iterator, _self, [&]( auto& row ) {
                row.ispaid = true;
                linksponsorawardtotreasure(iterator->treasurepkey);
            
                //TODO send fee amount to token holders dividend account
                //eosio::asset sponsorawardfee; //this amount is sent to token holders dividend account
            });
        }
        else if (memo.rfind("RCVT", 0) == 0) {
            //from account pays to check a treasure value
            uint64_t treasurepkey = std::strtoull( memo.substr(4).c_str(),NULL,0 );
            
            treasure_index treasures(_self, _self.value);
            auto iterator = treasures.find(treasurepkey);
            eosio_assert(iterator != treasures.end(), "Treasure not found.TODORemoveA.");

            //Tag the transfered amount on the treasure so the modtrchest-function can store the treasure value as an encrypted(hidden value)
            treasures.modify(iterator, _self, [&]( auto& row ) {
                row.prechesttransfer += eos;
            });
        }
    }
    //=====================================================================

    //Each treasur can have a linked sponsor award in addition to the treasure's token value
    //Sponsor awards are added by sponsors to a specific treasure. The treasure can only have one active sponsor award, so when several
    //sponsor awards are linked to a treasure they are stored in a queue until someone unlock a treasure and then activate the
    //next sponsor que in line. Only sponsor awards that are paid for will be added to a treasure.
    void linksponsorawardtotreasure(uint64_t treasurepkey) {
        treasure_index treasures(_self, _self.value);
        auto iterator = treasures.find(treasurepkey);
        
        if(iterator == treasures.end()){ return; } //Treasure not found. Quit this function
        if(iterator->sponsorawardvaluex2.amount > 0){ return; } //Treasure has currently an active sponsor award. Next sponsor award in line will be added next time the treasure is unlocked by a user.
        
        //Find first sponsor award in queue for this treasure pkey and copy award info to treasure
        sponsorqueue_index sponsorqueues(_self, _self.value);
        auto iteratorsponsorqueues = sponsorqueues.get_index<"treasurepkey"_n>(); //TODO: How to set lower bound
        
        //Copy sponsor award info to treasure and then remove the sponsor award from queue
        uint64_t nextsponsorqueuepkey = -1;
        eosio::name nextsponsorowner;
        std::string nextsponsortitle;
        std::string nextsponsorimageurl; 
        std::string nextsponsororderpageurl;
        eosio::asset nextsponsorawardvaluex2;  
        for (auto itr = iteratorsponsorqueues.lower_bound(treasurepkey); itr != iteratorsponsorqueues.end(); itr++) {
            if((*itr).ispaid == 1){ //Find the first available paid sponsor award to be linked to the treasure
                nextsponsorqueuepkey = (*itr).pkey;
                nextsponsorowner = (*itr).owner;
                nextsponsortitle = (*itr).sponsortitle;
                nextsponsorimageurl = (*itr).sponsorimageurl;
                nextsponsororderpageurl = (*itr).sponsororderpageurl;
                nextsponsorawardvaluex2 = (*itr).sponsorawardvaluex2;
                break;
            }
        }

        if(nextsponsorqueuepkey < 0){ return; }
        
        treasures.modify(iterator, _self, [&]( auto& row ) {
            row.sponsortitle = nextsponsortitle;
            row.sponsorimageurl = nextsponsorimageurl;
            row.sponsorowneraccount = nextsponsorowner;
            row.sponsororderpageurl = nextsponsororderpageurl;
            row.sponsorawardvaluex2 = nextsponsorawardvaluex2;

            //Remove sponsor award from queue
            //sponsoraward_index sponsorqueues(_code, _code.value);
            auto removeawarditerator = sponsorqueues.find(nextsponsorqueuepkey);
            if(removeawarditerator == sponsorqueues.end()){ return; } //Sponsor award does not exist.
            sponsorqueues.erase(removeawarditerator);
        });
    };


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
            row.sponsorawardvaluex2 = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.expirationdate = now() + 94608000; //Treasure ownership expires after three years if not reclaimed
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

    [[eosio::action]]
    void modtrchest(name user, uint64_t pkey, std::string treasurechestsecret, int32_t videoviews, asset totalturnover, name byuser) {
        //require_auth(user);
        require_auth("cptblackbill"_n);
        
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        
        //name oracleaccount = "cptblackbill"_n;
        //eosio_assert(user == oracleaccount, "Updating trcf is only allowed by CptBlackBill.");

        uint64_t rankingpoints = (videoviews * 1000) + totalturnover.amount;
        eosio::asset currentprechesttransfer = iterator->prechesttransfer;
        eosio::asset currenttotalturnover = iterator->totalturnover;
        name treasureowner = iterator->owner; 

        treasures.modify(iterator, user, [&]( auto& row ) {
            row.treasurechestsecret = treasurechestsecret; //This is the encrypted value of the treasure. It's not very hard to decrypt if someone finds that more exciting than reading the code on location. But for most of us it's easier to just pay $2 to get the treasure value.
            row.videoviews = videoviews;
            row.totalturnover = totalturnover;
            row.rankingpoints = rankingpoints;
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4)); //Clear this - amount has been encrypted and stored in the treasurechestsecret
        
            if(currentprechesttransfer.amount > 0)
            {
                eosio::asset fivepercenttotokenholders = (currentprechesttransfer * (5 * 100)) / 10000;
                action(
                    permission_level{ get_self(), "active"_n },
                    "eosio.token"_n, "transfer"_n,
                    std::make_tuple(get_self(), "cptbbpayout1"_n, fivepercenttotokenholders, std::string("five percent to token holders"))
                ).send();

                //Issue one new BLKBILL tokens to owner and payer for participating in CptBlackBill
                
                cptblackbill::issue(treasureowner, eosio::asset(10000, symbol(symbol_code("BLK"), 4)), std::string("Someone payed to check your treasure!") );
                send_summary(treasureowner, "1 BLK issued to you for someone checking your treasure.");

                cptblackbill::issue(byuser, eosio::asset(10000, symbol(symbol_code("BLK"), 4)), std::string("1 BLK issued to you for using CptBlackBill.") );
                send_summary(byuser, "1 BLK issued to you for using CptBlackBill.");
            }

            //If new total turnover is higher than current total turnover then the treasure has been unlocked by a finder.
            if(totalturnover > currenttotalturnover) //This makes it impossible to click unlock treasure several times in a row to get 10 BLKBILL for free. 
            {
                //Treasure has been unlocked by <byuser>. This has high participating value and both users get 10 new BLKBILL tokens

                //The EOS transactions that transfer the treasure chest value to finder and creator is
                //run as two separate transactions in the dapp (if not, the transaction is not visible on the receiver accounts) 
                
                cptblackbill::issue(treasureowner, eosio::asset(100000, symbol(symbol_code("BLK"), 4)), std::string("10 BLKBILL tokens for someone solving your treasure.") );
                send_summary(treasureowner, "10 BLKBILL tokens for someone solving your treasure.");

                cptblackbill::issue(byuser, eosio::asset(100000, symbol(symbol_code("BLK"), 4)), std::string("10 BLKBILL tokens as congrats for unlocking treasure!") );
                send_summary(byuser, "10 BLKBILL tokens as congrats for unlocking treasure!");

                //Remove current sponsor award info. This will open for adding the next sponsor award from queue
                row.sponsortitle = "";
                row.sponsorimageurl = "";
                row.sponsorowneraccount = ""_n;
                row.sponsororderpageurl = ""; 
                row.sponsorawardvaluex2 = eosio::asset(0, symbol(symbol_code("EOS"), 4)); 
            }
        });
        
        //Link next available sponsor award to this treasure. Must execute after Modify-code above.
        linksponsorawardtotreasure(pkey); 

        //Get token balance
        //asset pool_eos = eosio::token::get_balance("eosio.token"_n,get_self(), symbol_code("EOS"));
    }

    [[eosio::action]]
    void modexpdate(name user, uint64_t pkey) {
        //require_auth(user);
        require_auth("cptblackbill"_n);
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        
        //name oracleaccount = "cptblackbill"_n;
        //eosio_assert(user == oracleaccount, "Updating expiration date is only allowed by CptBlackBill."); //This is to make sure (verified gps location by CptBlackBill) that the owner has actually been on location and entered secret code

        treasures.modify(iterator, user, [&]( auto& row ) {
            row.expirationdate = now() + 94608000; //Treasure ownership renewed for three years
        });
    }

    [[eosio::action]]
    void erasetreasur(name user, uint64_t pkey) {
        require_auth(user);
        
        treasure_index treasures(_code, _code.value);
        
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure does not exist.");
        eosio_assert(user == iterator->owner || user == "cptblackbill"_n, "You don't have access to remove this treasure.");
        
        treasures.erase(iterator);
    }

    [[eosio::action]]
    void addaward(eosio::name user, eosio::name owner, uint64_t treasurepkey, std::string title, std::string imageurl, std::string orderpageurl, 
                  asset awardvaluex2, asset awardfee) 
    {
        require_auth(user);
        
        eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
        eosio_assert(imageurl.length() <= 100, "Max length of imageUrl is 100 characters.");
        eosio_assert(orderpageurl.length() <= 100, "Max length of orderpageurl is 100 characters.");

        sponsorqueue_index awards(_code, _code.value);
        
        awards.emplace(user, [&]( auto& row ) { //The user who run the transaction is RAM payer. So if added from CptBlackBill dapp, CptBlackBill is responsible for RAM.
            row.pkey = awards.available_primary_key();
            row.owner = owner; //The eos account that will get economic benefits when treasure is unlocked etc
            row.treasurepkey = treasurepkey;
            row.sponsortitle = title;
            row.sponsorimageurl = imageurl;
            row.sponsororderpageurl = orderpageurl;
            row.sponsorawardvaluex2 = awardvaluex2; //Award value in EOS times two.
            row.sponsorawardfee = awardfee; //Fee for adding a sponsor award (paid out to token holders).
            row.ispaid = false; //False until sponsor has paid by an EOS transfer
            row.timestamp = now();
        });
    }

    [[eosio::action]]
    void eraseaward(name user, uint64_t pkey) {
        require_auth(user);
        
        sponsorqueue_index awards(_code, _code.value);
        auto iterator = awards.find(pkey);
        eosio_assert(iterator != awards.end(), "Record does not exist");
        eosio_assert(user == iterator->owner || user == "cptblackbill"_n, "You don't have access to remove this sponsor award.");

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
        eosio::name owner;
        std::string title; 
        std::string description;
        std::string imageurl;
        std::string treasuremapurl;
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
        std::string sponsortitle;
        std::string sponsorimageurl; //Advertise image from sponsor. User who solves treasure will get this product/award 
        name sponsorowneraccount;
        std::string sponsororderpageurl; //sponsor image will link to this web-page. Users must be able to buy and winners must be able to claim award from this page.
        eosio::asset sponsorawardvaluex2; //the value of the award (x2 - times two) that will be transfered to the first treasure finder and the creator of the treasure.
        
        //uint64_t primary_key() const { return key.value; }
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_owner() const {return owner.value; } //second key, can be non-unique
    };
    //typedef eosio::multi_index<"treasure"_n, treasure> treasure_index;
    typedef eosio::multi_index<"treasure"_n, treasure, eosio::indexed_by<"owner"_n, const_mem_fun<treasure, uint64_t, &treasure::by_owner>>> treasure_index;

    struct [[eosio::table]] sponsorqueue {
        uint64_t pkey;
        eosio::name owner;
        uint64_t treasurepkey;
        std::string sponsortitle;
        std::string sponsorimageurl; 
        std::string sponsororderpageurl;
        eosio::asset sponsorawardvaluex2; //Value of the treasure award in EOS times two - since both finder and creator of treasure get equal amount 
        eosio::asset sponsorawardfee; //10 percent fee for adding sponsor award (payout to token holders).
        bool ispaid;
        std::string jsondata;  //additional field for other info in json format.
        int32_t timestamp; //Date created - queue order
        
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_owner() const {return owner.value; } //second key, can be non-unique
        uint64_t by_treasurepkey() const {return treasurepkey; } //third key, can be non-unique
    };
    //typedef eosio::multi_index<"sponsorqueue"_n, sponsorqueue> sponsorqueue_index;
    typedef eosio::multi_index<"sponsorqueue"_n, sponsorqueue, eosio::indexed_by<"owner"_n, const_mem_fun<sponsorqueue, uint64_t, &sponsorqueue::by_owner>>, eosio::indexed_by<"treasurepkey"_n, const_mem_fun<sponsorqueue, uint64_t, &sponsorqueue::by_treasurepkey>>> sponsorqueue_index;
    //typedef eosio::multi_index<"pollvotes"_n, pollvotes, eosio::indexed_by<"pollid"_n, eosio::const_mem_fun<pollvotes, uint64_t, &pollvotes::by_pollId>>> votes;

    //// local instances of the multi indexes
    //treasuretable _treasuretable;
    //awardqutable _awardqutable;

    void send_summary(name user, std::string message) {
        action(
            permission_level{get_self(),"active"_n},
            get_self(),
            "notify"_n,
            std::make_tuple(user, name{user}.to_string() + message)
        ).send();
    };


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
    else if(code==receiver && action==name("modexpdate").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modexpdate );
    }
    else if(code==receiver && action==name("erasetreasur").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasetreasur );
    }
    else if(code==receiver && action==name("addaward").value) {
      execute_action(name(receiver), name(code), &cptblackbill::addaward );
    }
    else if(code==receiver && action==name("eraseaward").value) {
      execute_action(name(receiver), name(code), &cptblackbill::eraseaward );
    }
    else if(code==receiver && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::transfer );
    }
    else if(code==name("eosio.token").value && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::onTransfer );
    }
  }
};

