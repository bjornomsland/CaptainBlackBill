var http = require('http');
var express = require('express');

http.createServer((request, response) => {
  
  const { headers, method, url } = request;
  let body = [];
  var transactionReturn = "";
  
  request.on('error', (err) => {

    response.writeHead(200, {'Content-Type': 'application/json'})
    const responseBody = { 'Error': err.message };
    response.write(JSON.stringify(transactionReturn));
    response.end();
  
  }).on('data', (jsonData) => {
    var jsonContent = JSON.parse(jsonData);    
    console.log('Received data');
    console.log(jsonContent);
    var returndata = {transactions: []}; //To use as return value
    
    Eos = require('eosjs');
    eos = Eos({
        httpEndpoint: jsonContent.httpEndpoint,
        chainId: jsonContent.chainId,
        keyProvider: jsonContent.keyProvider,
        expireInSeconds: 60,
        verbose: false
    });

    //Start first transaction
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
               console.log('First Transaction:');
               var jsonTrans1Result = JSON.parse(JSON.stringify(value));
               returndata.transactions.push({transno: "1", eostransid: jsonTrans1Result['processed']['id'], status: jsonTrans1Result['processed']['receipt']['status']});
               
               if(jsonContent.transactions.length > 1){
		  //===SECOND TRANSACTION===================================================
		   console.log('StartSecondTransaction');
		   eos.transaction(
            	   {
                      actions: [
                      {
                        account: jsonContent.transactions[1].account,
                        name: jsonContent.transactions[1].actionName,
                        authorization: [{
                          actor: jsonContent.transactions[1].actor,
                          permission: 'active'
                        }],
                        data: jsonContent.transactions[1].binArgs
                      }
                      ]
                    }).then(function (value) {
                      var jsonTrans2Result = JSON.parse(JSON.stringify(value));
                      returndata.transactions.push({transno: "2", eostransid: jsonTrans2Result['processed']['id'], status: jsonTrans2Result['processed']['receipt']['status']});
               
                      if(jsonContent.transactions.length > 2){
		      //===THIRD TRANSACTION===================================================
		        console.log('StartThirdTransaction');
                        eos.transaction(
            	        {
                          actions: [
                          {
                            account: jsonContent.transactions[2].account,
                            name: jsonContent.transactions[2].actionName,
                            authorization: [{
                              actor: jsonContent.transactions[2].actor,
                              permission: 'active'
                            }],
                            data: jsonContent.transactions[2].binArgs
                          }
                          ]
                        }).then(function (value) {
                           var jsonTrans3Result = JSON.parse(JSON.stringify(value));
                           returndata.transactions.push({transno: "3", eostransid: jsonTrans3Result['processed']['id'], status: jsonTrans3Result['processed']['receipt']['status']});
               
                           if(jsonContent.transactions.length > 3){
		           //===FOURTH TRANSACTION===================================================
		             console.log('StartFourthTransaction');
			     eos.transaction(
            	             {
                               actions: [
                               {
                                  account: jsonContent.transactions[3].account,
                                  name: jsonContent.transactions[3].actionName,
                                  authorization: [{
                                   actor: jsonContent.transactions[3].actor,
                                   permission: 'active'
                                  }],
                                  data: jsonContent.transactions[3].binArgs
                               }]
                             }).then(function (value) {
                                var jsonTrans4Result = JSON.parse(JSON.stringify(value));
                                returndata.transactions.push({transno: "4", eostransid: jsonTrans4Result['processed']['id'], status: jsonTrans4Result['processed']['receipt']['status']});
               
                                if(jsonContent.transactions.length > 4){
		                //===FIRTH TRANSACTION===================================================
		                console.log('StartFifthTransaction. Not implemented.');

		                //===ENDOFFIFTH=====================================================================
	                        }
                                else{
                                  console.log('SendResponseAfterFourthTransactionFinish');
                                  response.writeHead(200, {'Content-Type': 'application/json'})
                                  response.write(JSON.stringify(returndata));
                                  response.end();
                                }
                           });
		           //===ENDOFFOURTH=====================================================================
	                   }
                           else{
                              console.log('SendResponseAfterThirdTransactionFinish');
                              response.writeHead(200, {'Content-Type': 'application/json'})
                              response.write(JSON.stringify(returndata));
                              response.end();
                           }
                      });
		      //===ENDOFTHIRD=====================================================================
	              }
		      else{
                           console.log('SendResponseAfterSecondTransactionFinish');
                           response.writeHead(200, {'Content-Type': 'application/json'})
                           response.write(JSON.stringify(returndata));
                           response.end();
                      }
                   });
		  //===ENDOFSECOND=====================================================================
	          }
                  else{
                      console.log('SendResponseAfterFirstTransactionFinish');
		      response.writeHead(200, {'Content-Type': 'application/json'})
                      response.write(JSON.stringify(returndata));
                      response.end();
                  }
               }
           );
    }catch (e) {
       console.log('FirstTransactionFailed');
       console.log(e);
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
}).listen(3000)
