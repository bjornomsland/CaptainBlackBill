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

        if (memo.rfind("Activate Sponsor Award No.", 0) == 0) {
            //from account pays for activating a sponsor award

            uint64_t sponsorqueuepkey = std::strtoull( memo.substr(26).c_str(),NULL,0 ); //Find primary key from memo text

            sponsorqueue_index sponsorqueues(_self, _self.value);
            auto iterator = sponsorqueues.find(sponsorqueuepkey);
            eosio_assert(iterator != sponsorqueues.end(), "Sponsor award not found.");
            eosio_assert(eos >= getMinimumSponsorWardInEOS(), "Paid amount is below minimum sponsor award value.");
            eosio_assert(eos == (iterator->spawvaluex2 + iterator->spawfee), "Paid amount don't match total cost of sponsor award.");
            
            //Tag sponsor award as paid and link to treasure if treasure don't have an active sponsor award already
            sponsorqueues.modify(iterator, _self, [&]( auto& row ) {
                row.ispaid = true;
                
                //Send fee amount to token holders payout account
                action(
                    permission_level{ get_self(), "active"_n },
                    "eosio.token"_n, "transfer"_n,
                    std::make_tuple(get_self(), "cptbbpayout1"_n, iterator->spawfee, std::string("Fee for adding sponsor award goes to token holders."))
                ).send();

                //Sponsors get equal amount in BLKBILLs as the value of Sponsor Award in USD
                action(
                    permission_level{ get_self(), "active"_n },
                    "cptblackbill"_n, "issue"_n,
                    std::make_tuple(from, eosio::asset(getPriceInUSD(eos).amount, symbol(symbol_code("BLKBILL"), 4)), std::string("BLKBILLs for activating sponsor award."))
                ).send();
            });

            linksponsorawardtotreasure(iterator->treasurepkey);
        }
        else if (memo.rfind("Check Treasure No.", 0) == 0) {
            //from account pays to check a treasure value

            eosio_assert(eos >= getPriceForCheckTreasureValueInEOS(), "Transfered amount is below minimum price for checking treasure value.");
            
            uint64_t treasurepkey = std::strtoull( memo.substr(18).c_str(),NULL,0 ); //Find treasure pkey from transfer memo
            
            treasure_index treasures(_self, _self.value);
            auto iterator = treasures.find(treasurepkey);
            eosio_assert(iterator != treasures.end(), "Treasure not found.");

            //Tag the transfered amount on the treasure so the modtrchest-function can store the treasure value as an encrypted(hidden value)
            treasures.modify(iterator, _self, [&]( auto& row ) {
                row.prechesttransfer += eos;

                //If treasure is not activated (rankingpoint=0) and transfer is from owner, then activate treasure
                if(iterator->rankingpoint == 0 && iterator->owner == from){
                    row.rankingpoint = 1;
                } 
                
                //If treasure has a sponsor award that has not been activated. Run a random lottery with a chance of 1:100 to be activated 
                if(iterator->spawvaluex2.amount > 0 && iterator->spawisactive == 0)
                {
                    uint64_t seed = current_time() + from.value;
                    capi_checksum256 result;
                    sha256((char *)&seed, sizeof(seed), &result);
                    seed = result.hash[1];
                    seed <<= 32;
                    seed |= result.hash[0];
                    uint32_t randomnumber = (uint32_t)(seed % getRndSponsorActivationNumber()); //getRndSponsorActivationNumber is 1:x chance of sponsor award being activated 
                    row.jsondata = "RandomNumber: " + std::to_string(randomnumber);
                    if(randomnumber == 1){
                        row.spawisactive = 1;
                    }
                    
                    //row.jsondata = std::to_string(randomnumber);
                
                }
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
        if(iterator->spawvaluex2.amount > 0){ return; } //Treasure has currently an active sponsor award. Next sponsor award in line will be added next time the treasure is unlocked by a user.
        
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
                nextsponsortitle = (*itr).spawtitle;
                nextsponsorimageurl = (*itr).spawimageurl;
                nextsponsororderpageurl = (*itr).spaworderpageurl;
                nextsponsorawardvaluex2 = (*itr).spawvaluex2;
                break;
            }
        }

        if(nextsponsorqueuepkey < 0){ return; }
        
        treasures.modify(iterator, _self, [&]( auto& row ) {
            row.spawtitle = nextsponsortitle;
            row.spawimageurl = nextsponsorimageurl;
            row.spawowner = nextsponsorowner;
            row.spaworderpageurl = nextsponsororderpageurl;
            row.spawvaluex2 = nextsponsorawardvaluex2;
            row.spawisactive = 0; //Sponsor award are not included in treasure right away. It will be added by random when someone pays for checking the treasure value

            //Remove sponsor award from queue
            auto removeawarditerator = sponsorqueues.find(nextsponsorqueuepkey);
            if(removeawarditerator == sponsorqueues.end()){ return; } //Sponsor award does not exist.
            sponsorqueues.erase(removeawarditerator);
        });
    };

    [[eosio::action]]
    void addtreasure(eosio::name user, eosio::name owner, std::string title, std::string imageurl, 
                     double latitude, double longitude, std::string treasurechestsecret) 
    {
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
            row.owner = owner;
            row.title = title;
            row.imageurl = imageurl;
            row.latitude = latitude;
            row.longitude = longitude;
            row.treasurechestsecret = treasurechestsecret;
            row.totalturnover = eosio::asset(0, symbol(symbol_code("EOS"), 4)); 
            row.sellingprice = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.spawvaluex2 = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.prechesttransfer = eosio::asset(0, symbol(symbol_code("EOS"), 4));
            row.expirationdate = now() + 94608000; //Treasure ownership expires after three years if not reclaimed
            row.spawisactive = 0;
            row.timestamp = now();
        });
    }

    [[eosio::action]]
    void modtreasure(name user, uint64_t pkey, std::string title, std::string description, std::string imageurl, 
                     std::string videourl, std::string category, int32_t level) 
    {
        require_auth( user );
        treasure_index treasures(_code, _code.value);
        auto iterator = treasures.find(pkey);
        eosio_assert(iterator != treasures.end(), "Treasure not found");
        eosio_assert(user == iterator->owner || user == "cptblackbill"_n, "You don't have access to modify this treasure.");

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
                send_summary(byuser, "10 BLKBILL tokens as congrats for unlocking a treasure!");
                
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
                    send_summary(treasureowner, "Congrats! This is bonus tokens for creating great content at CptBlackBill!"); 
                }
                else{
                    if(rankingpoints > 1000)
                        bonusPayout = 10000000; //1000 BLKBILLs
                    else if (rankingpoints > 100)
                        bonusPayout = 1000000; //100 BLKBILLs
                    else
                        bonusPayout = 100000; //10 BLKBILLs

                    cptblackbill::issue(treasureowner, eosio::asset(bonusPayout, symbol(symbol_code("BLKBILL"), 4)), std::string("10 BLKBILLs for someone solving your treasure.") );
                    send_summary(treasureowner, "10 BLKBILLs for someone solving your treasure.");
                } 
                
                //Remove current sponsor award info. This will open for adding the next sponsor award from queue
                row.spawtitle = "";
                row.spawimageurl = "";
                row.spawowner = ""_n;
                row.spaworderpageurl = ""; 
                row.spawvaluex2 = eosio::asset(0, symbol(symbol_code("EOS"), 4));
                row.spawisactive = 0;
                //row.jsondata = std::to_string(bonusPayout);
                
                //Update 2018-12-28 Add user who unlocked tresure to the result table for easy access on scoreboard in dapp
                results_index results(_code, _code.value);
                results.emplace(_self, [&]( auto& row ) { 
                    row.pkey = results.available_primary_key();
                    row.user = byuser; //The eos account that found and unlocked the treasure
                    row.treasurepkey = pkey;
                    row.payouteos = thisTurnover;
                    row.eosusdprice = getEosUsdPrice(); //2019-01-08
                    row.minedblkbills = eosio::asset(bonusPayout, symbol(symbol_code("BLKBILL"), 4));
                    row.timestamp = now();
                }); 
            }
        });
        
        //Link next available sponsor award to this treasure. Must execute after Modify-code above.
        linksponsorawardtotreasure(pkey); 

        //Get token balance
        //asset poolEOS = eosio::token::get_balance("eosio.token"_n,get_self(), symbol_code("EOS"));
    }

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
            row.spawtitle = title;
            row.spawimageurl = imageurl;
            row.spaworderpageurl = orderpageurl;
            row.spawvaluex2 = awardvaluex2; //Award value in EOS times two.
            row.spawfee = awardfee; //Fee for adding a sponsor award (paid out to token holders).
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
    void eraseresult(name user, uint64_t pkey) {
        require_auth("cptblackbill"_n);
        
        results_index results(_code, _code.value);
        auto iterator = results.find(pkey);
        eosio_assert(iterator != results.end(), "Result does not exist.");
        results.erase(iterator);
    }

    [[eosio::action]]
    void upsertcrew(name user, std::string imagehash, std::string quote) 
    {
        require_auth( user );
        crewinfo_index crewinfo(_code, _code.value);
        auto iterator = crewinfo.find(user.value);
        if( iterator == crewinfo.end() )
        {
            crewinfo.emplace(user, [&]( auto& row ) {
                row.user = user;
                row.imagehash = imagehash;
                row.quote = quote;
            });
        }
        else {
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
        uint64_t rankingpoint; //Calculated and updated by CptBlackBill based on video and turnover stats.  
        int32_t timestamp; //Date created
        int32_t expirationdate; //Date when ownership expires - other users can then take ownnership of this treasure location
        std::string treasurechestsecret;
        std::string jsondata;  //additional field for other info in json format.
        std::string spawtitle;
        std::string spawimageurl; //Advertise image from sponsor. User who solves treasure will get this product/award 
        name spawowner;
        std::string spaworderpageurl; //sponsor image will link to this web-page. Users must be able to buy and winners must be able to claim award from this page.
        eosio::asset spawvaluex2; //the value of the award (x2 - times two) that will be included in the treasure when sponsorAwardIsIncludedInTreasure = true.
        bool spawisactive; //To prevent a finder from getting the next sponsor award on location by just entering the secret code several times, the sponsor award has a random chance of 1:100 to be activated. 
        //uint64_t primary_key() const { return key.value; }
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_owner() const {return owner.value; } //second key, can be non-unique
        uint64_t by_rankingpoint() const {return rankingpoint; } //fourth key, can be non-unique
    };
    typedef eosio::multi_index<"treasure"_n, treasure, 
            eosio::indexed_by<"owner"_n, const_mem_fun<treasure, uint64_t, &treasure::by_owner>>,
            eosio::indexed_by<"rankingpoint"_n, const_mem_fun<treasure, uint64_t, &treasure::by_rankingpoint>>> treasure_index;

    struct [[eosio::table]] sponsorqueue {
        uint64_t pkey;
        eosio::name owner;
        uint64_t treasurepkey;
        std::string spawtitle;
        std::string spawimageurl; 
        std::string spaworderpageurl;
        eosio::asset spawvaluex2; //Value of the treasure award in EOS times two - since both finder and creator of treasure get equal amount 
        eosio::asset spawfee; //10 percent fee for adding sponsor award (payout to token holders).
        bool ispaid;
        std::string jsondata;  //additional field for other info in json format.
        int32_t timestamp; //Date created - queue order
        
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_owner() const {return owner.value; } //second key, can be non-unique
        uint64_t by_treasurepkey() const {return treasurepkey; } //third key, can be non-unique
    };
    typedef eosio::multi_index<"sponsorqueue"_n, sponsorqueue, 
            eosio::indexed_by<"owner"_n, const_mem_fun<sponsorqueue, uint64_t, &sponsorqueue::by_owner>>, 
            eosio::indexed_by<"treasurepkey"_n, const_mem_fun<sponsorqueue, uint64_t, &sponsorqueue::by_treasurepkey>>> sponsorqueue_index;
    
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
        std::string trxid;
        eosio::asset payouteos;
        eosio::asset eosusdprice;
        eosio::asset minedblkbills;
        int32_t timestamp; //Date created - queue order
        
        uint64_t primary_key() const { return  pkey; }
        uint64_t by_user() const {return user.value; } //second key, can be non-unique
        uint64_t by_treasurepkey() const {return treasurepkey; } //third key, can be non-unique
    };
    typedef eosio::multi_index<"results"_n, results, 
            eosio::indexed_by<"user"_n, const_mem_fun<results, uint64_t, &results::by_user>>, 
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

    uint32_t getRndSponsorActivationNumber() {
        uint32_t rndSponsorActivation = 100; //default value 100. Meaning it's a 1:100 chance of next sponsor award being activated when a treasure is unlocked
        
        //Get settings from table if exists. If not, default value is used
        settings_index settings(_self, _self.value);
        auto iterator = settings.find(name("rndsponsorac").value); 
        if(iterator != settings.end()){
            rndSponsorActivation = iterator->uintvalue;    
        }
        
        return rndSponsorActivation;
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

    asset getMinimumSponsorWardInEOS() {
        asset minimumSponsorWardInEOS = eosio::asset(10000, symbol(symbol_code("EOS"), 4)); //default minimum sponsor award is 1 EOS
        
        //Get settings from table if exists. If not, default value is used
        settings_index settings(_self, _self.value);
        auto iterator = settings.find(name("minsponsoraw").value); 
        if(iterator != settings.end()){
            minimumSponsorWardInEOS = iterator->assetvalue;    
        }
        
        return minimumSponsorWardInEOS;
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
    else if(code==receiver && action==name("addsetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::addsetting );
    }
    else if(code==receiver && action==name("modsetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::modsetting );
    }
    else if(code==receiver && action==name("erasesetting").value) {
      execute_action(name(receiver), name(code), &cptblackbill::erasesetting );
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

