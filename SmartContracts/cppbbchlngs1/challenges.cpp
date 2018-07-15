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
                     string& imageUrl, double& latitude, double& longitude) {
                
                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");
                
                //Check if it starts with http
	              eosio_assert(imageUrl.find("http") == 0, "Missing valid image URL. Must start with lower case 'http'");
                
                //Check if location is valid. (And don't allow lat=0 long=0 - known as Null Island) 
                bool locationIsValid = true;
                if((latitude < -90 || latitude > 90) || latitude == 0) {
                    locationIsValid = false;
                }

                if((longitude < -180 || longitude > 180) || longitude == 0){
                    locationIsValid = false;
                }
                
                eosio_assert(locationIsValid, "Location (latitude and/ord longitude) is not valid.");
                
                //Access the challenge table as creating an object of type "challengeIndex"
                challengeIndex challenges(_self, _self);  //Code scope
                
                //Add new challenge
                challenges.emplace(account, [&](auto& challenge) {
                    challenge.pkey = challenges.available_primary_key();
                    challenge.storagePayerAccountNo = account; //The account that pays for RAM storage
                    challenge.storagePayerAccountName = name{account}; //Readable name for account who pays for RAM storage 
                    challenge.editorsAccountName = name{editorsAccount}; //Usually the same as the account that pays for storage, but can be another account if you want to give update access to another account
                    challenge.title = title;
                    challenge.imageUrl = imageUrl; 
                    challenge.latitude = latitude;
                    challenge.longitude = longitude;
                    challenge.totalturnover = asset(0, string_to_symbol(4, "BLKBILL"));
                    challenge.adstotalturnover = asset(0, string_to_symbol(4, "BLKBILL"));
                    challenge.timestamp = now();

                    print("NewChallengePkey: ", challenge.pkey); 
                });
            }

            //@abi action
            void updtitle(const account_name account, uint64_t pkey, string& title) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");

                eosio_assert(title.length() <= 55, "Max length of title is 55 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.title = title;
                    print("UpdateTitleOk");
                });
            }

            //@abi action
            void updimgurl(const account_name account, uint64_t pkey, string& imageUrl) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");

                eosio_assert(imageUrl.find("http") == 0, "Invalid image URL. Must start with lower case 'http'");
                eosio_assert(imageUrl.length() <= 100, "Max length of image url is 100 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.imageUrl = imageUrl;
                    print("UpdateImageOk");
                });
            }

            //@abi action
            void updimg2url(const account_name account, uint64_t pkey, string& imageUrl) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");

                //Convert imageUrl to lower case and check if it starts with http
	            eosio_assert(imageUrl.find("http") == 0, "Invalid image URL. Must start with lower case 'http'");
                eosio_assert(imageUrl.length() <= 100, "Max length of image url is 100 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.additionalImageUrl = imageUrl;
                    print("UpdateAdditionalImageOk");
                });
            }

            //@abi action
            void updvideourl(const account_name account, uint64_t pkey, string& videoUrl) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(videoUrl.find("http") == 0, "Invalid video URL. Must start with lower case 'http'");
                eosio_assert(videoUrl.length() <= 100, "Max length of video url is 100 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.videoUrl = videoUrl;
                    print("UpdateVideoUrlOk");
                });
            }

            //@abi action
            void updblogurl(const account_name account, uint64_t pkey, string& blogUrl) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(blogUrl.find("http") == 0, "Invalid blog URL. Must start with lower case 'http'");
                eosio_assert(blogUrl.length() <= 100, "Max length of video url is 100 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.blogUrl = blogUrl;
                    print("UpdateBlogUrlOk");
                });
            }

            //@abi action
            void updgpxurl(const account_name account, uint64_t pkey, string& gpxTrackUrl) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(gpxTrackUrl.find("http") == 0, "Invalid GPX track URL. Must start with lower case 'http'");
                eosio_assert(gpxTrackUrl.length() <= 100, "Max length of gpxtrack url is 100 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.gpxTrackUrl = gpxTrackUrl;
                    print("UpdateGpxTrackUrlOk");
                });
            }


            //@abi action
            void updsecretcod(const account_name account, uint64_t pkey, string& secretcode) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                
                eosio_assert(secretcode.length() == 4, "Secret code must be four characters long and only contain letters and numbers.");
                
                //Add some simple encryption to hide the secret challenge code. There is a number of reasons not
                //to make the secret code to difficult to decrypt:
                //1. Users must be at the correct GPS-location when secret code is validated. So getting there is more difficult.
                //2. A secret code on location can have been removed, destroyed or damages. Then decrypting the secret code is part of the game. 
                string encryptedCode = "";
                string key = std::to_string(account); 
                for(int v=0;v<=3;v++)
                {
                    if(v == 0)
                      encryptedCode = std::to_string(int(secretcode[v]^key[v]));
                    else
                      encryptedCode += "-" + std::to_string(int(secretcode[v]^key[v]));
                }
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.encryptedSecretCode = encryptedCode;
                    print("The secret code has been updated.");
                });
            }

            //@abi action
            void updlevel(const account_name account, uint64_t pkey, int32_t& level) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                
                bool levelIsValid = true;
                if(level <= 0 || level > 10)
                    levelIsValid = false;

                eosio_assert(levelIsValid, "Invalid grade level. Level can be from 1 (very easy) to 10 (very hard).");
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.level = level;
                    print("UpdateGradeLevelOk");
                });
            }

            //@abi action
            void updcategory(const account_name account, uint64_t pkey, string& category) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(category.length() <= 50, "Max length of category is 50 characters.");
                
                std::transform(category.begin(), category.end(), category.begin(), ::tolower);

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.category = category;
                    print("UpdateCategoryOk");
                });
            }

            //@abi action
            void upddescr(const account_name account, uint64_t pkey, string& description) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(description.length() <= 650, "Max length of description is 650 characters.");
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.description = description;
                    print("UpdateDescriptionOk");
                });
            }

            //@abi action
            void updtags(const account_name account, uint64_t pkey, string& tags) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(tags.length() <= 50, "Max length of tags is 50 characters.");

                std::transform(tags.begin(), tags.end(), tags.begin(), ::tolower);

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.tags = tags;
                    print("UpdateTagsOk");
                });
            }

            //@abi action
            void upddatajson(const account_name account, uint64_t pkey, string& datajson) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(datajson.length() <= 500, "Max length of datajson is 500 characters.");

                challenges.modify(iterator, account, [&](auto& challenge) {
                    eosio_assert(account == challenge.storagePayerAccountNo, "Account is not allowed to update this Challenge.");
                    challenge.dataJson = datajson;
                    print("UpdateAdditionalJsonDataOk");
                });
            }

            //@abi action
            void updvideocnt(const account_name account, uint64_t pkey, int32_t& videoviewcount, int32_t& videolikecount) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                
                account_name oracleAccount1 = N(cptbbfinanc1); //Account that is allowed to update video statistics
                account_name oracleAccount2 = N(cptbboracle1); //Account that is allowed to update video statistics
                
                bool isValidOracle = false;
                if(account == oracleAccount1 || account == oracleAccount2)
                    isValidOracle = true;
                
                eosio_assert(isValidOracle, "Updating video statistics is only allowed by Oracle accounts.");
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    challenge.videoviewcount = videoviewcount;
                    challenge.videolikecount = videolikecount;
                    print("UpdateVideoCountStatisticsOk");
                });
            }

            //@abi action
            void updturnover(const account_name account, uint64_t pkey, 
                             asset& totalturnover, asset& adstotalturnover, int32_t& rankingpoints) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                account_name oracleAccount1 = N(cptbbfinanc1); //Account that is allowed to update video statistics
                account_name oracleAccount2 = N(cptbboracle1); //Account that is allowed to update video statistics

                bool isValidOracle = false;
                if(account == oracleAccount1 || account == oracleAccount2)
                    isValidOracle = true;

                asset defaultasset = asset(0, string_to_symbol(4, "BLKBILL"));

                eosio_assert(isValidOracle, "Updating turnover statistics is only allowed by Oracle accounts.");
                eosio_assert(totalturnover.symbol == defaultasset.symbol, "Token symbol for Total Turnover must in BLKBILL.");
                eosio_assert(adstotalturnover.symbol == defaultasset.symbol, "Token symbol for Total Ads Turnover must be BLKBILL.");
                
                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                
                challenges.modify(iterator, account, [&](auto& challenge) {
                    challenge.totalturnover = totalturnover;
                    challenge.adstotalturnover = adstotalturnover;
                    challenge.rankingpoints = rankingpoints;
                    print("UpdateTotalTurnoverStatisticsAndRankingPointsOk");
                });
            }

            //@abi action
            void remove(const account_name account, uint64_t pkey) {

                require_auth(account); //Only owner of account, or somebody with the account authorization, can use this action

                challengeIndex challenges(_self, _self);
                auto iterator = challenges.find(pkey);
                eosio_assert(iterator != challenges.end(), "Challenge not found");
                eosio_assert(account == challenges.get(pkey).storagePayerAccountNo, "Account is not allowed to remove this Challenge.");
  
                challenges.erase(iterator); 

                print("RemoveChallengeOK"); 
            }

            
            void bylatitude(uint32_t latitude) {
                print("ByLatitude"); 
            }

        private:
            //@abi table challenges i64
            struct challenge {
                uint64_t pkey;
                eosio::name editorsAccountName; //The account who added/created this Challenge (readable text format)
                uint64_t storagePayerAccountNo; //The account who pays for RAM storage of this Challenge (numeric format)
                eosio::name storagePayerAccountName; //The account who pays for RAM storage of this Challenge (readable text format)
                std::string title;
                std::string tags; //Optional tag words that describe this Challenge
                std::string description;
                std::string imageUrl; //Main image that is used to present the Trasure Challenge.
                std::string additionalImageUrl; //Optional image to give more details about how to solve the Challenge.
                std::string videoUrl; //Link to video (Must be a video provider that support API to views and likes)
                std::string blogUrl; 
                std::string gpxTrackUrl; //Link to a gps track file (GPX format)
                std::string category; //Climbing, biking, hiking, cross-country-skiing, etc
                std::string encryptedSecretCode; //The code (encrypted) that other users must type in to prove that they have solved the challenge. Normally 4-6 in length with letters (case sensitive) and numbers
                double latitude = 0; //GPS coordinate
                double longitude = 0; //GPS coordinate 
                int32_t level = 0; //Difficulty Rating. Value 1-10 where 1 is very easy and 10 is very hard.
                int32_t videoviewcount = 0; //Updated from Oracle 
                int32_t videolikecount = 0; //Updated from Oracle 
                asset totalturnover; //Total historical value in BLCKBLs that has been paid out to users from the TC was made public. Updated from Oracle
                asset adstotalturnover; //Total historical value in BLCKBLs that has been used from sponsors to promote prizes from the TC was made public. Updated from Oracle
                int32_t rankingpoints = 0; //Calculated and updated from Oracle based on video and turnover stats.  
                int32_t timestamp;
                std::string dataJson; //Can be used for other data if needed.
                
                uint64_t primary_key() const { return  pkey; }
                double by_latitude() const { return latitude; }
                uint64_t by_videoviews() const { return videoviewcount; }

                                
                EOSLIB_SERIALIZE(challenge, (pkey)(editorsAccountName)(storagePayerAccountNo)(storagePayerAccountName)
                                             (title)(tags)(description)(imageUrl)(additionalImageUrl)(videoUrl)(blogUrl)
                                             (gpxTrackUrl)(category)(encryptedSecretCode)(latitude)(longitude)(level)
                                             (videoviewcount)(videolikecount)
                                             (totalturnover)(adstotalturnover)(rankingpoints)(timestamp)(dataJson))
            };

            //typedef multi_index<N(challenge), challenge> challengeIndex;

            //typedef multi_index<N(challenges), challenge,
            //        indexed_by< N(latitude), const_mem_fun<challenge, uint64_t, &challenge::by_latitude>
            //        >
            //    > challengeIndex;

            //typedef eosio::multi_index<N(mystruct), mystruct, 
            //         indexed_by<N(latitude), const_mem_fun<mystruct, uint64_t, &mystruct::by_id>>, indexed_by<N(anotherid), const_mem_fun<mystruct, uint64_t, &mystruct::by_anotherid>>> datastore;


            //typedef eosio::multi_index<N(challenges), challenge, 
            //            indexed_by<N(latitude), const_mem_fun<challenge, uint64_t, &challenge::by_latitude>>,
            //            indexed_by<N(videoviewcount), const_mem_fun<mystruct, uint64_t, &mystruct::by_videoviews>> challengeIndex;


            typedef multi_index<N(challenges), challenge,
                    indexed_by< N(latitude), const_mem_fun<challenge, double, &challenge::by_latitude>>,
                    indexed_by< N(videoviewcount), const_mem_fun<challenge, uint64_t, &challenge::by_videoviews>>
                > challengeIndex;
    };

    EOSIO_ABI(Challenge, (add)(updtitle)(updimgurl)(updimg2url)(updvideourl)(updblogurl)
                          (updgpxurl)(updlevel)(updsecretcod)
                          (updcategory)(upddescr)(updtags)(upddatajson)(updvideocnt)(updturnover)(remove))
}