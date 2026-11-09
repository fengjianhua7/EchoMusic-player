const fs=require('fs'),vm=require('vm'),assert=require('assert');
class Element {constructor(){this.children=[];this.value='';this.textContent='';this.disabled=false;this.hidden=false;this.dataset={};}append(...xs){this.children.push(...xs)}replaceChildren(...xs){this.children=[...xs]}setAttribute(k,v){this[k]=v}}
const elements=new Map(),posts=[];const get=id=>{if(!elements.has(id))elements.set(id,new Element());return elements.get(id)};
const context={document:{getElementById:get,createElement:()=>new Element(),querySelectorAll:()=>[]},URLSearchParams,console,setTimeout:()=>{},fetch:async(path,opts)=>{posts.push([path,opts]);return {ok:true,text:async()=>'',json:async()=>({})}}};
vm.createContext(context);let source=fs.readFileSync('work/library-ui.js','utf8').replace(/refresh\(\);\s*$/,'');vm.runInContext(source,context);
const files=Array.from({length:128},(_,i)=>'Track '+i+'.mp3');files[17]='阿桑 - 温柔的慈悲.mp3';files[127]='阿桑 - 测试 &amp; 现场.mp3';
context.fixture={localFiles:files,localFileSizes:Array(128).fill(1048576),localChanged:true,localRevision:1,favoriteRevision:3,favoriteSongs:['喜欢的歌'],favoritePage:1,favoriteHasNext:true,cardStatus:'列表已更新',volume:25,songs:['<script>alert(1)</script>'],selected:0,loggedIn:true,playing:false,paused:false,favoriteStatus:'第 1 页',nowTitle:'测试'};
vm.runInContext('render(fixture)',context);assert.equal(get('localFiles').children.length,128);
get('localQuery').value='阿桑';vm.runInContext('renderLocal()',context);assert.equal(get('localFiles').children.length,2);
get('localFiles').children[1].children[2].onclick();assert.equal(posts.at(-1)[1].body.get('index'),'127');
get('localQuery').value='NO MATCH';vm.runInContext('renderLocal()',context);assert.equal(get('localFiles').children.length,1);assert.ok(get('localFiles').children[0].textContent.includes('没有匹配'));
get('localQuery').value='';context.fixture={...context.fixture,localChanged:false};delete context.fixture.localFiles;delete context.fixture.localFileSizes;vm.runInContext('render({...fixture})',context);vm.runInContext('renderLocal()',context);assert.equal(get('localFiles').children.length,128);
assert.equal(get('songs').children[0].children[1].children[0].textContent,'<script>alert(1)</script>');
get('favoriteSongs').children[0].children[2].onclick();assert.equal(posts.at(-1)[1].body.get('revision'),'3');
console.log('UI behavior passed: Chinese filtering, original index 127, empty results, incremental state cache, safe title text and favorite revision.');
