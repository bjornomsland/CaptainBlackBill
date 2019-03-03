var http = require('http');
var express = require('express');

http.createServer((request, response) => {

    const { headers, method, url } = request;
    let body = [];
    var transactionReturn = "";

    request.on('error', (err) => {

        response.writeHead(200, { 'Content-Type': 'application/json' })
        const responseBody = { 'Error': err.message };
        response.write(JSON.stringify(transactionReturn));
        response.end();

    }).on('data', (jsonData) => {
        var jsonContent = JSON.parse(jsonData);
        console.log('Received data');
        console.log(jsonContent);
        var returndata = { transactions: [] }; //To use as return value

        Eos = require('eosjs');
        eos = Eos({
            httpEndpoint: jsonContent.httpEndpoint,
            chainId: jsonContent.chainId,
            keyProvider: jsonContent.keyProvider,
            expireInSeconds: 60,
            verbose: false
        });

        if (jsonContent.description == 'createNewAccount') {

            try {
                var Ecc = require('eosjs-ecc');
                Ecc.randomKey().then(privateKey => {
                    var publicKey = Ecc.privateToPublic(privateKey);

                    console.log("PublicKey: " + publicKey);

                    eos.transaction(tr => {
                        tr.newaccount({
                            creator: 'cptblackbill',
                            name: jsonContent.newAccountName,
                            owner: publicKey,
                            active: publicKey
                        })

                        tr.buyrambytes({
                            payer: 'cptblackbill',
                            receiver: jsonContent.newAccountName,
                            bytes: 3692
                        })

                        tr.delegatebw({
                            from: 'cptblackbill',
                            receiver: jsonContent.newAccountName,
                            stake_net_quantity: jsonContent.stakeNetQuantity,
                            stake_cpu_quantity: jsonContent.stakeCpuQuantity,
                            transfer: 0
                        })
			
			tr.transfer({
			    from: 'cptblackbill', 
   		            to: jsonContent.newAccountName, 
                            quantity: jsonContent.transferEOSAmount, 
                            memo: 'Welcome to Cpt.BlackBill.'
			})


                    }).then(function (value) {
                        console.log('NewAccountExecuted: ' + publicKey);
                        var jsonTrans1Result = JSON.parse(JSON.stringify(value));
                        returndata.transactions.push({ transno: "1", eostransid: jsonTrans1Result['processed']['id'], privateKey: privateKey, publicKey: publicKey, status: jsonTrans1Result['processed']['receipt']['status'] });

                        console.log('SendResponse');
                        response.writeHead(200, { 'Content-Type': 'application/json' })
                        response.write(JSON.stringify(returndata));
                        response.end();
                    });
                
                });
            } catch (e) {
                console.log('TransactionFailed');
                returndata.transactions.push({ transno: "1", eostransid: '', privateKey: '', publicKey: '', status: e.message });
                response.writeHead(200, { 'Content-Type': 'application/json' })
                response.write(JSON.stringify(returndata));
                response.end();
            }
        }
        else if (jsonContent.description == 'createNewAccountWithPublicKey') {

            try {
                
                eos.transaction(tr => {
                    tr.newaccount({
                        creator: 'cptblackbill',
                        name: jsonContent.newAccountName,
                        owner: jsonContent.publicKey,
                        active: jsonContent.publicKey
                    })

                    tr.buyrambytes({
                        payer: 'cptblackbill',
                        receiver: jsonContent.newAccountName,
                        bytes: 3692
                    })

                    tr.delegatebw({
                        from: 'cptblackbill',
                        receiver: jsonContent.newAccountName,
                        stake_net_quantity: jsonContent.stakeNetQuantity,
                        stake_cpu_quantity: jsonContent.stakeCpuQuantity,
                        transfer: 0
                    })

                    //tr.transfer({
                    //    from: 'cptblackbill',
                    //    to: jsonContent.newAccountName,
                    //    quantity: jsonContent.transferEOSAmount,
                    //    memo: 'Welcome to Cpt.BlackBill.'
                    //})


                }).then(function (value) {
                    console.log('NewAccountExecuted: ' + jsonContent.publicKey);
                    var jsonTrans1Result = JSON.parse(JSON.stringify(value));
                    returndata.transactions.push({ transno: "1", eostransid: jsonTrans1Result['processed']['id'], publicKey: jsonContent.publicKey, status: jsonTrans1Result['processed']['receipt']['status'] });

                    console.log('SendResponse');
                    response.writeHead(200, { 'Content-Type': 'application/json' })
                    response.write(JSON.stringify(returndata));
                    response.end();
                });

            } catch (e) {
                console.log('TransactionFailed');
                returndata.transactions.push({ transno: "1", eostransid: '', publicKey: '', status: e.message });
                response.writeHead(200, { 'Content-Type': 'application/json' })
                response.write(JSON.stringify(returndata));
                response.end();
            }
        }
        else {

            console.log('RunActions');

            try {
                eos.transaction(
                    {
                        actions: [
                            {
                                account: jsonContent.transactions[0].account,
                                name: jsonContent.transactions[0].actionName,
                                authorization: [{
                                    actor: jsonContent.transactions[0].actor,
                                    permission: 'active'
                                }],
                                data: jsonContent.transactions[0].binArgs
                            }
                        ]
                    }).then(function (value) {
                        console.log('TransactionExecuted:');
                        var jsonTrans1Result = JSON.parse(JSON.stringify(value));
                        returndata.transactions.push({ transno: "1", eostransid: jsonTrans1Result['processed']['id'], status: jsonTrans1Result['processed']['receipt']['status'] });

                        console.log('SendResponse');
                        response.writeHead(200, { 'Content-Type': 'application/json' })
                        response.write(JSON.stringify(returndata));
                        response.end();
                    }
                    );
            } catch (e) {
                console.log('TransactionFailed');
                console.log(e);
            }
        }

    }).on('end', () => {
        console.log('No more data received.');

        response.on('error', (err) => {
            console.error('Error in end-function');
            console.error(err);
        });

        //  response.writeHead(200, {'Content-Type': 'application/json'})
        //  const responseBody = { 'Message':'Timeout on 15 seconds.','Data':'No action performed.' };
        //  response.write(JSON.stringify(responseBody));
        //  response.end();

    });
}).listen(3000)// JavaScript source code
