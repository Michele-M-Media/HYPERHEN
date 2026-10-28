/* Actual socket responses for the production parent health verifier. */
const assert=require('node:assert/strict');
const http=require('node:http');
const {spawn}=require('node:child_process');
const {once}=require('node:events');
const executable=process.argv[2];
async function probe(){
  const child=spawn(executable,['--probe'],{stdio:'ignore'});
  return (await once(child,'exit'))[0];
}
async function main(){
  const existing=await new Promise(resolve=>{
    const req=http.get('http://127.0.0.1:8088/api/health',res=>{res.resume();resolve(true);});
    req.on('error',()=>resolve(false));
  });
  assert(!existing,'Port 8088 occupied; no existing listener will be touched');
  assert.equal(await probe(),1);
  const good='{"service":"hyperdeck-websrv","protocol":1}';
  let response=good,status=200,split=false;
  const server=http.createServer((req,res)=>{
    assert.equal(req.url,'/api/health');assert.equal(req.headers.host,'127.0.0.1:8088');
    res.writeHead(status,{'Content-Length':Buffer.byteLength(response)});
    if(split){
      res.write(response.slice(0,10));
      setTimeout(()=>res.end(response.slice(10)),20);
    }else res.end(response);
  });
  server.listen(8088,'127.0.0.1');await once(server,'listening');
  try{
    assert.equal(await probe(),0);
    split=true;assert.equal(await probe(),0);split=false;
    for(const invalid of ['{"service":"other","protocol":1}','{"service":"hyperdeck-websrv","protocol":2}','{"service":"hyperdeck-websrv","protocol":10}','{"service":"hyperdeck-websrv"']){
      response=invalid;assert.equal(await probe(),1);
    }
    response=good;status=500;assert.equal(await probe(),1);
  }finally{await new Promise(resolve=>server.close(resolve));}
  console.log('HyperDeck HTTP verifier: real listener, split response, wrong service/protocol, truncated JSON and failed status checks passed.');
}
main().catch(error=>{console.error(error);process.exitCode=1;});
