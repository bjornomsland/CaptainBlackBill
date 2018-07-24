#include "challenges.hpp"

namespace CptBlackBill {
    using namespace eosio;
    using namespace std;
    using std::string;

    class Challenge : public contract {
        using contract::contract;
        public:
            Challenge(account_name self):contract(self) {}
            
            //@abi action
            void add(const account_name account, const account_name editorsAccount, string& title, 
                     string& imageUrl, double& latitude, double& longitude, string& tcrf) {
                
                require_auth(account);
                eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
                eosio_assert(imageUrl.length() <= 100, "Max length of imageUrl is 100 characters.");
                //eosio_assert(imageUrl.find("https") == 0, "Missing valid image URL. Must start with lower case 'https'");
                
                bool locationIsValid = true;
                if((latitude < -90 || latitude > 90) || latitude == 0) {
                    locationIsValid = false;
                }

                if((longitude < -180 || longitude > 180) || longitude == 0){
                    locationIsValid = false;
                }
                
                eosio_assert(locationIsValid, "Location (latitude and/ord longitude) is not valid.");
                challengeIndex challenges(_self, _self);
                
                challenges.emplace(account, [&](auto& challenge) {
                    challenge.pkey = challenges.available_primary_key();
                    challenge.editorsAccount = name{editorsAccount}; //Usually the same as the account that pays for storage, but can be another account if you want to give update access to another account
                    challenge.storagePayerAccount = name{account}; //The account that pays for RAM storage
                    challenge.title = title;
                    challenge.imageUrl = imageUrl;
                    challenge.tcrf = tcrf;
                    challenge.latitude = latitude;
                    challenge.longitude = longitude;
                    challenge.totalturnover = asset(0, string_to_symbol(4, "BLKBILL"));
                    challenge.timestamp = now();
                    //print("NewChallengePkey: ", challenge.pkey); 
                });
            }

            //@abi action
            void update(const account_name account, uint64_t pkey, string& title, 
                        string& description, string& imageUrl, string& videoUrl,
                        string& category, int32_t& level) {

                require_auth(account);
                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
                eosio_assert(description.length() <= 650, "Max length of description is 650 characters.");
                //eosio_assert(imageUrl.find("https") == 0, "Invalid image URL. Must be from a secure server and start with lower case 'https'");
                eosio_assert(imageUrl.length() <= 100, "Max length of image url is 100 characters.");
                //eosio_assert(videoUrl.find("https") == 0, "Invalid video URL. Must be from a secure server and start with lower case 'https'");
                eosio_assert(videoUrl.length() <= 100, "Max length of video url is 100 characters.");
                eosio_assert(category.length() <= 50, "Max length of category is 50 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccount, "Account is not allowed to update this Challenge.");
                    challenge.title = title;
                    challenge.description = description;
                    challenge.imageUrl = imageUrl;
                    challenge.videoUrl = videoUrl;
                    challenge.category = category;
                    challenge.level = level;
                    //print("UpdateTitleOk");
                });
            }

            //@abi action
            void updtcrf(const account_name account, uint64_t pkey, string& tcrf, int32_t& videoviews, asset& totalturnover) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                
                account_name oracleAccount1 = N(cptbbfinanc1); //Account that is allowed to update video statistics
                
                //asset defaultasset = asset(0, string_to_symbol(4, "BLKBILL"));
                //eosio_assert(totalturnover.symbol == defaultasset.symbol, "Token symbol for Total Turnover must in BLKBILL.");
                
                eosio_assert(account == oracleAccount1, "Updating trcf is only allowed by Oracle account.");
                
                uint64_t rankingpoints = (videoviews * 1000) + totalturnover.amount;
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    challenge.tcrf = tcrf;
                    challenge.videoviews = videoviews;
                    challenge.totalturnover = totalturnover;
                    challenge.rankingpoints = rankingpoints;
                    //print("UpdateTcrfOk");
                });
            }

            //@abi action
            void remove(const account_name account, uint64_t pkey) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(account == challenges.get(pkey).storagePayerAccount, "Account is not allowed to remove this Challenge.");
  
                challenges.erase(iterator); 

                //print("RemoveChallengeOK"); 
            }

        private:
            //@abi table challenges i64
            struct challenge {
                uint64_t pkey;
                eosio::name editorsAccount; //The account who added/created this Challenge (readable text format)
                eosio::name storagePayerAccount;
                std::string title;
                std::string description;
                std::string imageUrl; //Main image that is used to present the Trasure Challenge.
                std::string videoUrl; //Link to video (Must be a video provider that support API to views and likes)
                std::string category; //Climbing, biking, hiking, cross-country-skiing, etc
                double latitude; //GPS coordinate
                double longitude; //GPS coordinate 
                int32_t level; //Difficulty Rating. Value 1-10 where 1 is very easy and 10 is very hard.
                int32_t videoviews; //Updated from Oracle 
                asset totalturnover; //Total historical value in BLCKBLs that has been paid out to users from the TC was made public. Updated from Oracle
                uint64_t rankingpoints; //Calculated and updated from Oracle based on video and turnover stats.  
                int32_t timestamp;
                std::string tcrf;
                                
                uint64_t primary_key() const { return  pkey; }
                                
                EOSLIB_SERIALIZE(challenge, (pkey)(editorsAccount)(storagePayerAccount)
                                             (title)(description)(imageUrl)(videoUrl)
                                             (category)(latitude)(longitude)(level)
                                             (videoviews)(totalturnover)(rankingpoints)(timestamp)(tcrf))
            };

            typedef multi_index<N(challenges), challenge> challengeIndex;

            /*typedef multi_index<N(challenges), challenge,
                    indexed_by< N(latitude), const_mem_fun<challenge, double, &challenge::by_latitude>>,
                    indexed_by< N(videoviewcount), const_mem_fun<challenge, uint64_t, &challenge::by_videoviews>>
                > challengeIndex;*/
    };

    EOSIO_ABI(Challenge, (add)(update)(updtcrf)(remove))
}