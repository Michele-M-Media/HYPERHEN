/* Build a self-contained offline preview from the production-layout fixtures. */
const fs=require('node:fs');
const path=require('node:path');
const target=process.argv[2];
if(!target) throw new Error('Usage: node tools/package_hyperdeck_preview.cjs OUTPUT.html');
const root=path.resolve(__dirname,'../assets/previews');
const frames=fs.readFileSync(path.join(root,'hyperdeck-frames.js'),'utf8');
const html=fs.readFileSync(path.join(root,'hyperdeck.html'),'utf8')
  .replace('<script src="hyperdeck-frames.js"></script>','<script>'+frames+'</script>');
fs.writeFileSync(target,html);
console.log('Offline HyperDeck preview: '+target);
