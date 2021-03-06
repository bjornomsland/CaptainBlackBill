#include "cptblackbill.hpp"

using namespace eosio;

class [[eosio::contract]] cptblackbill : public eosio::contract {

public:
    using contract::contract;
    
    cptblackbill(name receiver, name code,  datastream<const char*> ds): contract(receiver, code, ds) {}
    
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

    static asset get_balance(name token_contract_account, name owner, symbol_code sym_code) {
        accounts accountstable(token_contract_account, owner.value);
        const auto& ac = accountstable.get(sym_code.raw());
        return ac.balance;
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

        if (memo.rfind("Check Treasure No.", 0) == 0) {
            //from account pays to check a treasure value

            eosio_assert(eos >= getPriceForCheckTreasureValueInEOS(), "Transfered amount is below minimum price for checking treasure value.");
            
            uint64_t treasurepkey = std::strtoull( memo.substr(18).c_str(),NULL,0 ); //Find treasure pkey from transfer memo
            
            treasure_index treasures(_self, _self.value);
            auto iterator = treasures.find(treasurepkey);
            eosio_assert(iterator != treasures.end(), "Treasure not found.");

            //Add row to verifycheck
            verifycheck_index verifycheck(_self, _self.value);
            verifycheck.emplace(_self, [&]( auto& row ) {
                row.pkey = verifycheck.available_primary_key();
                row.treasurepkey = treasurepkey;
                row.byaccount = from;
                row.timestamp = now();
            });

            //Tag the transfered amount on the treasure so the modtrchest-function can store the treasure value as an encrypted(hidden value)
            treasures.modify(iterator, _self, [&]( auto& row ) {
                row.prechesttransfer += eos;

                //If treasure is not activated (rankingpoint=0) and transfer is from owner, then activate treasure
                if(iterator->rankingpoint == 0 && iterator->owner == from){
                    row.rankingpoint = 1;
                } 
            });
        }
        if (memo.rfind("Unlock Treasure No.", 0) == 0) {
            //from account pays to unlock a treasure

            eosio_assert(eos >= getPriceForCheckTreasureValueInEOS(), "Transfered amount is below minimum price for unlocking a treasure.");
            
            //Get treasurepkey and secret code from memo
            replace(memo, "Unlock Treasure No.", "");
            std::size_t delimiterlocation = memo.find("-");
            uint64_t treasurepkey = std::strtoull( memo.substr(0, delimiterlocation).c_str(),NULL,0 ); 
            std::string secretcode = memo.substr(delimiterlocation + 1);

            //Add row to verifyunlock
            verifyunlock_index verifyunlock(_self, _self.value);
            verifyunlock.emplace(_self, [&]( auto& row ) {
                row.pkey = verifyunlock.available_primary_key();
                row.treasurepkey = treasurepkey;
                row.secretcode = secretcode;
                row.byaccount = from;
                row.timestamp = now();
            });
        }
    }
    //=====================================================================

    bool replace(std::string& str, const std::string& from, const std::string& to) {
        size_t start_pos = str.find(from);
        if(start_pos == std::string::npos)
            return false;
        str.replace(start_pos, from.length(), to);
        return true;
    }

    [[eosio::action]]
    void addtreasure(eosio::name owner, std::string title, std::string imageurl, 
                     double latitude, double longitude) 
    {
        require_auth(owner);
        
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
        
        treasures.emplace(owner, [&]( auto& row ) {
            row.pkey = treasures.available_primary_key();
            row.owner = owner;
            row.title = title;
            row.imageurl = imageurl;
            row.latitude = latitude;
            row.longitude = longitude;
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.expirationdate = now() + 94608000; //Treasure expires after three years if not found
            row.timestamp = now();
        });
    }

    [[eosio::action]]
    void modtreasure(name user, uint64_t pkey, std::string title, std::string description, std::string imageurl, 
                     std::string videourl) 
    {
        require_auth( user );
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        eosio_assert(user == iterator->owner || user == "cptblackbill"_n, "You don't have access to modify this treasure.");

        eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
        eosio_assert(description.length() <= 650, "Max length of description is 650 characters.");
        eosio_assert(imageurl.length() <= 100, "Max length of image url is 100 characters.");
        eosio_assert(videourl.length() <= 100, "Max length of video url is 100 characters.");
        
        treasures.modify(iterator, user, [&]( auto& row ) {
            row.title = title;
            row.description = description;
            row.imageurl = imageurl;
            row.videourl = videourl;
        });
    }

/*
    [[eosio::action]]
    void modtrchest(name user, uint64_t pkey, std::string treasurechestsecret, int32_t videoviews, asset totalturnover, name byuser) {
        require_auth("cptblackbill"_n); //Only allowed by cptblackbill contract

        //TODO
        //TreasureChestSecret is encrypted in the DAPP together with the hidden treasure amount. These values must be 
        //hidden from users or else the secret code is visible on the blockchain and the user has less insentive to pay for checking the treasure value
        //The project privEOS (https://www.slant.li/priveos/) can maybe solve this problem in the future (estimated Q3 2019 from privEOS)

        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");

        uint64_t bonusPayout = 0;    
        uint64_t rankingpoints = pow( (getPriceInUSD(totalturnover).amount / 10000), 0.8); //Turnover value gives exponential ranking points
        if(videoviews > 0){
            rankingpoints = rankingpoints * pow(videoviews, 0.3); //Number of video views has exponential power, but less than turnover   
        }

        //Updated 2018-december 28
        //If current rankingpoints is 1 or higher the treasure has been activated and can not be set back to 0 ranking points
        if(rankingpoints <= 0 && iterator->rankingpoint > 0){
            rankingpoints = 1; //Never set ranking points back to zero if treasure is activated.
        } 
        
        eosio::asset currentprechesttransfer = iterator->prechesttransfer;
        eosio::asset currenttotalturnover = iterator->totalturnover;
        eosio::asset thisTurnover = (totalturnover - iterator->totalturnover); //This is the current treasure chest value and will be paid out equally to the finder and the creator
        eosio::asset currentSpawValueX2 = iterator->spawvaluex2;
        name treasureowner = iterator->owner; 

        treasures.modify(iterator, user, [&]( auto& row ) {
            row.treasurechestsecret = treasurechestsecret; //This is the encrypted value of the treasure. It's not very hard to decrypt if someone finds that more exciting than reading the code on location. But for most of us it's easier to just pay $2 to get the treasure value.
            row.videoviews = videoviews;
            row.totalturnover = totalturnover;
            row.rankingpoint = rankingpoints;
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4)); //Clear this - amount has been encrypted and stored in the treasurechestsecret
            
            if(currentprechesttransfer.amount > 0) //Someone has paid (transeferd EOS to CptBlackBill) to check a treasure value
            {
                //Transfer five percent of the transfered EOS to the token holders payout account
                eosio::asset fivepercenttotokenholders = (currentprechesttransfer * (5 * 100)) / 10000;
                action(
                    permission_level{ get_self(), "active"_n },
                    "eosio.token"_n, "transfer"_n,
                    std::make_tuple(get_self(), "cptbbpayout1"_n, fivepercenttotokenholders, std::string("Five percent cut to token holders payout account"))
                ).send();

                //Issue one new BLKBILL tokens to owner and payer for participating in CptBlackBill
                cptblackbill::issue(treasureowner, eosio::asset(10000, symbol(symbol_code("BLKBILL"), 4)), std::string("Someone paid to check your treasure!") );
                send_summary(treasureowner, "1 BLKBILL to you for someone checking your treasure value.");

                cptblackbill::issue(byuser, eosio::asset(10000, symbol(symbol_code("BLKBILL"), 4)), std::string("1 BLKBILL to you for using CptBlackBill.") );
                send_summary(byuser, "1 BLKBILL to you for using CptBlackBill.");
            }

            //If new total turnover is higher than current total turnover then the treasure has been unlocked by a finder.
            //if(totalturnover > currenttotalturnover) //This makes it impossible to click unlock treasure several times in a row to get 10 BLKBILL for free. 
            if(thisTurnover.amount > 0) 
            {
                //Treasure has been unlocked by <byuser>. 

                //Transfer treasure chest value to the user who unlocked the treasure
                action(
                    permission_level{ get_self(), "active"_n },
                    "eosio.token"_n, "transfer"_n,
                    std::make_tuple(get_self(), byuser, thisTurnover, std::string("Congrats for solving Treasure No." + std::to_string(pkey) + " on CptBlackBill!"))
                ).send();

                //Transfer the same amount to the user who created the treasure
                action(
                    permission_level{ get_self(), "active"_n },
                    "eosio.token"_n, "transfer"_n,
                    std::make_tuple(get_self(), treasureowner, thisTurnover, std::string("Congrats! Your Treasure No." + std::to_string(pkey) + " has been solved. This is your equal share of the treasure chest."))
                ).send();

                //Reward finder for using CptBlackBill
                cptblackbill::issue(byuser, eosio::asset(100000, symbol(symbol_code("BLKBILL"), 4)), std::string("10 BLKBILL tokens as congrats for unlocking treasure!") );
                //send_summary(byuser, "10 BLKBILL tokens as congrats for unlocking a treasure!");
                
                //Reward creator for creating content 
                //BONUS TOKENS TO CREATORS
                asset poolBlkBill = get_balance("cptblackbill"_n, get_self(), symbol_code("BLKBILL"));
                bonusPayout = pow( (getPriceInUSD(thisTurnover).amount), 1.2) * rankingpoints; //Update 2018-12-30 Removed /10000 on getPriceInUsd
                if(bonusPayout > 100000000)
                    bonusPayout = 100000000; //Max 10000.0000 BLKBILL in bonus payout
                
                if(bonusPayout > 0 && (poolBlkBill.amount - 100000000) > bonusPayout) //Subtract a 10000 BLKBILL buffer
                {
                    action(
                        permission_level{ get_self(), "active"_n },
                        "cptblackbill"_n, "transfer"_n,
                        std::make_tuple(get_self(), treasureowner, eosio::asset(bonusPayout, symbol(symbol_code("BLKBILL"), 4)), std::string("Congrats! This is bonus tokens for creating great content at CptBlackBill!"))
                    ).send();
                    //send_summary(treasureowner, "Congrats! This is bonus tokens for creating great content at CptBlackBill!"); 
                }
                else{
                    if(rankingpoints > 5000)
                        bonusPayout = 2000000; //200 BLKBILLs
                    else if (rankingpoints > 1000)
                        bonusPayout = 1000000; //100 BLKBILLs
                    else
                        bonusPayout = 100000; //10 BLKBILLs

                    cptblackbill::issue(treasureowner, eosio::asset(bonusPayout, symbol(symbol_code("BLKBILL"), 4)), std::string("10 BLKBILLs for someone solving your treasure.") );
                    //send_summary(treasureowner, "10 BLKBILLs for someone solving your treasure.");
                } 
                
             }
        });
        
        //Get token balance
        //asset poolEOS = eosio::token::get_balance("eosio.token"_n,get_self(), symbol_code("EOS"));
    }
*/

    [[eosio::action]]
    void modexpdate(name user, uint64_t pkey) {
        require_auth("cptblackbill"_n); //"Updating expiration date is only allowed by CptBlackBill. This is to make sure (verified gps location by CptBlackBill) that the owner has actually been on location and entered secret code
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        
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
    void addsetting(name keyname, std::string stringvalue, asset assetvalue, uint32_t uintvalue) 
    {
        require_auth("cptblackbill"_n);
        
        settings_index settings(_code, _code.value);
        
        settings.emplace(_self, [&]( auto& row ) { //The user who run the transaction is RAM payer. So if added from CptBlackBill dapp, CptBlackBill is responsible for RAM.
            row.keyname = keyname; // pkey = settings.available_primary_key();
            //row.key = key; 
            row.stringvalue = stringvalue;
            row.assetvalue = assetvalue;
            row.uintvalue = uintvalue;
            row.timestamp = now();
        });
    }
    
    [[eosio::action]]
    void modsetting(name keyname, std::string stringvalue, asset assetvalue, uint32_t uintvalue) 
    {
        require_auth("cptblackbill"_n);
        settings_index settings(_code, _code.value);
        auto iterator = settings.find(keyname.value);
        eosio_assert(iterator != settings.end(), "Setting not found");
        
        settings.modify(iterator, _self, [&]( auto& row ) {
            row.stringvalue = stringvalue;
            row.assetvalue = assetvalue;
            row.uintvalue = uintvalue;
            row.timestamp = now();
        });
    }

    [[eosio::action]]
    void erasesetting(name keyname) {
        require_auth("cptblackbill"_n);
        
        settings_index settings(_code, _code.value);
        auto iterator = settings.find(keyname.value);
        eosio_assert(iterator != settings.end(), "Setting does not exist");
        settings.erase(iterator);
    }

    [[eosio::action]]
    void eraseverchk(uint64_t pkey) {
        require_auth("cptblackbill"_n);
        
        verifycheck_index verifycheck(_code, _code.value);
        auto iterator = verifycheck.find(pkey);
        eosio_assert(iterator != verifycheck.end(), "Verify check value record does not exist");
        verifycheck.erase(iterator);
    }

    [[eosio::action]]
    void eraseverunlc(uint64_t pkey) {
        require_auth("cptblackbill"_n);
        
        verifyunlock_index verifyunlock(_code, _code.value);
        auto iterator = verifyunlock.find(pkey);
        eosio_assert(iterator != verifyunlock.end(), "Verify unlock record does not exist");
        verifyunlock.erase(iterator);
    }

    [[eosio::action]]
    void eraseresult(name user, uint64_t pkey) {
        require_auth("cptblackbill"_n);
        
        results_index results(_code, _code.value);
        auto iterator = results.find(pkey);
        eosio_assert(iterator != results.end(), "Result does not exist.");
        results.erase(iterator);
    }

    [[eosio::action]]
    void upsertcrew(name user, name crewmember, std::string imagehash, std::string quote) 
    {
        require_auth( user );
        crewinfo_index crewinfo(_code, _code.value);
        auto iterator = crewinfo.find(crewmember.value);
        if( iterator == crewinfo.end() )
        {
            eosio_assert(user == crewmember || user == "cptblackbill"_n, "Only Cpt.BlackBill can insert crewmembers on behalf of other users.");
            crewinfo.emplace(user, [&]( auto& row ) {
                row.user = crewmember;
                row.imagehash = imagehash;
                row.quote = quote;
            });
        }
        else {
            eosio_assert(user == iterator->user || user == "cptblackbill"_n, "You don't have access to modify this crewmember.");
            crewinfo.modify(iterator, user, [&]( auto& row ) {
                row.imagehash = imagehash;
                row.quote = quote;
            });
        }
    }

    [[eosio::action]]
    void erasecrew(name user) {
        require_auth( user );
        
        crewinfo_index crewinfo(_code, _code.value);
        auto iterator = crewinfo.find(user.value);
        eosio_assert(iterator != crewinfo.end(), "Crew-info does not exist.");
        crewinfo.erase(iterator);
    }

    [[eosio::action]]
    void runpayout(name user) {
        require_auth("cptbbpayout1"_n);
       
        
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
        double latitude; //GPS coordinate
        double longitude; //GPS coordinate
        eosio::asset prechesttransfer; //Used when someone pay for checking treasure value. Token value is stored here until cptblackbill add tokens to encrypted treasure value
        uint64_t rankingpoint; //Calculated and updated by CptBlackBill based on video and turnover stats.  
        int32_t timestamp; //Date created
        int32_t expirationdate; //Date when ownership expires - other users can then take ownnership of this treasure location
        std::string status;
        std::string jsondata;  //additional field for other info in json format.
        //uint64_t primary_key() const { return key.value; }
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_owner() const {return owner.value; } //second key, can be non-unique
        uint64_t by_rankingpoint() const {return rankingpoint; } //fourth key, can be non-unique
    };
    typedef eosio::multi_index<"treasure"_n, treasure, 
            eosio::indexed_by<"owner"_n, const_mem_fun<treasure, uint64_t, &treasure::by_owner>>,
            eosio::indexed_by<"rankingpoint"_n, const_mem_fun<treasure, uint64_t, &treasure::by_rankingpoint>>> treasure_index;

    struct [[eosio::table]] verifycheck {
        uint64_t pkey;
        uint64_t treasurepkey;
        eosio::name byaccount;
        int32_t timestamp;

        uint64_t primary_key() const { return  pkey; }
    };
    typedef eosio::multi_index<"verifycheck"_n, verifycheck> verifycheck_index;

    struct [[eosio::table]] verifyunlock {
        uint64_t pkey;
        uint64_t treasurepkey;
        eosio::name byaccount;
        std::string secretcode;
        eosio::name treasureowner;
        eosio::asset payouteos;
        eosio::asset eosusdprice;
        int32_t timestamp;

        uint64_t primary_key() const { return  pkey; }
    };
    typedef eosio::multi_index<"verifyunlock"_n, verifyunlock> verifyunlock_index;

    struct [[eosio::table]] settings {
        eosio::name keyname; 
        std::string stringvalue;
        eosio::asset assetvalue;
        uint32_t uintvalue;
        int32_t timestamp; //last updated
        
        uint64_t primary_key() const { return keyname.value; }
    };
    typedef eosio::multi_index<"settings"_n, settings> settings_index;
    
    struct [[eosio::table]] results {
        uint64_t pkey;
        uint64_t treasurepkey;
        eosio::name user;
        eosio::name creator;
        std::string trxid;
        eosio::asset payouteos;
        eosio::asset eosusdprice;
        eosio::asset minedblkbills;
        int32_t timestamp; //Date created - queue order
        
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_user() const {return user.value; } //second key, can be non-unique
        uint64_t by_creator() const {return creator.value; } //third key, can be non-unique
        uint64_t by_treasurepkey() const {return treasurepkey; } //fourth key, can be non-unique
    };
    typedef eosio::multi_index<"results"_n, results, 
            eosio::indexed_by<"user"_n, const_mem_fun<results, uint64_t, &results::by_user>>, 
            eosio::indexed_by<"creator"_n, const_mem_fun<results, uint64_t, &results::by_creator>>, 
            eosio::indexed_by<"treasurepkey"_n, const_mem_fun<results, uint64_t, &results::by_treasurepkey>>> results_index;

    struct [[eosio::table]] crewinfo {
        eosio::name user;
        std::string imagehash;
        std::string quote;
        
        uint64_t primary_key() const { return  user.value; }
    };
    typedef eosio::multi_index<"crewinfo"_n, crewinfo> crewinfo_index;

    void send_summary(name user, std::string message) {
        action(
            permission_level{get_self(),"active"_n},
            get_self(),
            "notify"_n,
            std::make_tuple(user, name{user}.to_string() + message)
        ).send();
    };

    //---Get dapp settings---------------------------------------------------------------------------------
    asset getEosUsdPrice() {
        asset eosusd = eosio::asset(0, symbol(symbol_code("USD"), 4)); //default value
        
        //Get settings from table if exists. If not, default value is used
        settings_index settings(_self, _self.value);
        auto iterator = settings.find(name("eosusd").value); 
        if(iterator != settings.end()){
            eosusd = iterator->assetvalue;    
        }
        return eosusd;
    };
    
    asset getPriceInUSD(asset eos) {
        asset eosusd = eosio::asset(27600, symbol(symbol_code("USD"), 4)); //default value
        
        //Get settings from table if exists. If not, default value is used
        settings_index settings(_self, _self.value);
        auto iterator = settings.find(name("eosusd").value); 
        if(iterator != settings.end()){
            eosusd = iterator->assetvalue;    
        }
                 
        uint64_t priceUSD = (eos.amount * eosusd.amount) / 10000;
        return eosio::asset(priceUSD, symbol(symbol_code("USD"), 4));
    };

    asset getPriceForCheckTreasureValueInEOS() {
        asset eosusd = eosio::asset(27600, symbol(symbol_code("USD"), 4)); //default value
        asset priceForCheckingTreasureValueInUSD = eosio::asset(20000, symbol(symbol_code("USD"), 4)); //default value for checking a treasure chest value
        
        //Get settings from table if exists. If not, default value is used
        settings_index settings(_self, _self.value);
        auto iterator = settings.find(name("eosusd").value); 
        if(iterator != settings.end()){
            eosusd = iterator->assetvalue;    
        }
        
        auto iterator2 = settings.find(name("checktreasur").value); 
        if(iterator2 != settings.end()){
            priceForCheckingTreasureValueInUSD = iterator2->assetvalue;    
        }
                 
        uint64_t priceInEOS = (priceForCheckingTreasureValueInUSD.amount / eosusd.amount) / 10000; //TODO: Check if this results in a larger amont of EOS and should be divided more
        return eosio::asset(priceInEOS, symbol(symbol_code("EOS"), 4));
    };
    //-----------------------------------------------------------------------------------------------------

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
    else if(code==receiver && action==name("modexpdate").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modexpdate );
    }
    else if(code==receiver && action==name("erasetreasur").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasetreasur );
    }
    else if(code==receiver && action==name("addsetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::addsetting );
    }
    else if(code==receiver && action==name("modsetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modsetting );
    }
    else if(code==receiver && action==name("erasesetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasesetting );
    }
    else if(code==receiver && action==name("eraseverchk").value) {
      execute_action(name(receiver), name(code), &cptblackbill::eraseverchk );
    }
    else if(code==receiver && action==name("eraseverunlc").value) {
      execute_action(name(receiver), name(code), &cptblackbill::eraseverunlc );
    }
    else if(code==receiver && action==name("eraseresult").value) {
      execute_action(name(receiver), name(code), &cptblackbill::eraseresult );
    }
    else if(code==receiver && action==name("upsertcrew").value) {
      execute_action(name(receiver), name(code), &cptblackbill::upsertcrew );
    }
    else if(code==receiver && action==name("erasecrew").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasecrew );
    }
    else if(code==receiver && action==name("issue").value) {
      execute_action(name(receiver), name(code), &cptblackbill::issue );
    }
    else if(code==receiver && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::transfer );
    }
    else if(code==name("eosio.token").value && action==name("transfer").value) {
      execute_action(name(receiver), name(code), &cptblackbill::onTransfer );
    }
  }
};

