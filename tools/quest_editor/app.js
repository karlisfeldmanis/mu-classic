/* MU Quest Editor — CMS-style admin interface */

const CLASS_NAMES = ['Dark Wizard','Dark Knight','Fairy Elf','Magic Gladiator'];
const CLASS_KEYS = ['dw','dk','elf','mg'];
const CAT_NAMES = {0:'Swords',1:'Axes',2:'Maces',3:'Spears',4:'Bows',5:'Staffs',6:'Shields',7:'Helms',8:'Armor',9:'Pants',10:'Gloves',11:'Boots',12:'Wings',13:'Accessory',14:'Consumables'};
const MONSTERS = {0:'Bull Fighter',1:'Hound',2:'Budge Dragon',3:'Spider',4:'Elite Bull Fighter',5:'Hell Hound',6:'Lich',7:'Giant',8:'Poison Bull',9:'Thunder Lich',10:'Dark Knight',11:'Ghost',12:'Larva',13:'Hell Spider',14:'Skeleton Warrior',15:'Skeleton Archer',16:'Skeleton Captain',17:'Cyclops',18:'Gorgon',19:'Yeti',20:'Elite Yeti',21:'Assassin',22:'Ice Monster',23:'Hommerd',24:'Worm',25:'Ice Queen',26:'Goblin',27:'Chain Scorpion',28:'Beetle Monster',29:'Hunter',30:'Forest Monster',31:'Agon',32:'Stone Golem',33:'Elite Goblin',34:'Cursed Wizard',35:'Death Gorgon',36:'Shadow',37:'Devil',38:'Balrog',39:'Poison Shadow',40:'Death Knight',41:'Death Cow'};

let db=null, items=[], quests=[], rewards={}, targets={}, originalData={};
let currentLoc='all', pendingSlot=null, editingQid=null;

const $=id=>document.getElementById(id);
const saveBtn=$('save-btn'), badge=$('status-badge');
const content=$('content'), emptyState=$('empty-state');
const modal=$('item-modal'), searchInput=$('item-search');
const itemList=$('item-list'), levelInput=$('item-level-input');
const panel=$('detail-panel');

// Init
saveBtn.onclick=onSave;
searchInput.oninput=()=>renderItemList();
$('item-clear-btn').onclick=onClear;
$('dp-close').onclick=closePanel;
$('dp-apply').onclick=applyDetail;
$('dp-add-target').onclick=()=>addTargetRow();
document.querySelector('.modal-close').onclick=closeModal;
document.querySelectorAll('.fbtn').forEach(b=>{
  b.onclick=()=>{document.querySelectorAll('.fbtn').forEach(x=>x.classList.remove('active'));b.classList.add('active');renderItemList();}
});
document.addEventListener('keydown',e=>{if(e.key==='Escape'){if(!modal.classList.contains('hidden'))closeModal();}});

// Auto-load
(async()=>{
  try{
    const r=await fetch('/api/db');if(!r.ok)return;
    const buf=await r.arrayBuffer();
    const SQL=await initSqlJs({locateFile:f=>`https://cdnjs.cloudflare.com/ajax/libs/sql.js/1.10.3/${f}`});
    db=new SQL.Database(new Uint8Array(buf));
    loadData();renderAll();saveBtn.disabled=false;status('Connected','saved');
  }catch(e){console.warn('AutoLoad:',e.message);}
})();

function status(t,type){badge.textContent=t;badge.className='status '+type;}

// ── Data ──
function loadData(){
  items=[];quests=[];rewards={};targets={};
  try{const r=db.exec("SELECT id,category,item_index,name,level_req,class_flags FROM item_definitions ORDER BY category,item_index");if(r.length)for(const row of r[0].values)items.push({id:row[0],cat:row[1],idx:row[2],name:row[3],lvlReq:row[4],cls:row[5],def:row[1]*32+row[2]});}catch(e){}
  try{const r=db.exec("SELECT quest_id,guard_npc_type,quest_name,location,lore_text FROM quest_definitions ORDER BY quest_id");if(r.length)for(const row of r[0].values)quests.push({id:row[0],guard:row[1],name:row[2],loc:row[3]||'Unknown',lore:row[4]||''});}catch(e){}
  try{const r=db.exec("SELECT quest_id,target_index,monster_type,kills_required FROM quest_targets ORDER BY quest_id,target_index");if(r.length)for(const row of r[0].values){if(!targets[row[0]])targets[row[0]]=[];targets[row[0]].push({ti:row[1],mt:row[2],kc:row[3]});}}catch(e){}
  try{const r=db.exec("SELECT quest_id,class_index,reward_slot,def_index,item_level FROM quest_rewards ORDER BY quest_id,class_index,reward_slot");if(r.length)for(const row of r[0].values){const[q,c,s,d,l]=row;if(!rewards[q])rewards[q]={};if(!rewards[q][c])rewards[q][c]={};rewards[q][c][s]={def:d,lvl:l};}}catch(e){}
  originalData={rewards:JSON.parse(JSON.stringify(rewards)),targets:JSON.parse(JSON.stringify(targets)),quests:JSON.parse(JSON.stringify(quests))};
}

// ── Render ──
function renderAll(){
  emptyState.classList.add('hidden');content.classList.remove('hidden');
  renderNav();renderTable();
}

function renderNav(){
  const locs={};for(const q of quests)locs[q.loc]=(locs[q.loc]||0)+1;
  const nav=$('nav-list');nav.innerHTML='';
  mkNav(nav,'All Quests',quests.length,'all');
  for(const[loc,cnt]of Object.entries(locs).sort())mkNav(nav,loc,cnt,loc);
}
function mkNav(parent,label,count,loc){
  const d=document.createElement('div');
  d.className='nav-item'+(currentLoc===loc?' active':'');
  d.innerHTML=`${label}<span class="nav-count">${count}</span>`;
  d.onclick=()=>{currentLoc=loc;renderNav();renderTable();};
  parent.appendChild(d);
}

function renderTable(){
  const filtered=currentLoc==='all'?quests:quests.filter(q=>q.loc===currentLoc);
  let html='<table class="quest-table"><thead><tr>';
  html+='<th>ID</th><th>Quest Name</th><th>Targets</th><th>Location</th>';
  for(let c=0;c<4;c++)html+=`<th><span class="class-hdr ${CLASS_KEYS[c]}">${CLASS_NAMES[c]}</span></th>`;
  html+='</tr></thead><tbody>';
  for(const q of filtered){
    const mod=isQuestModified(q.id)?'modified':'';
    const tgts=(targets[q.id]||[]).map(t=>`<span class="kill">${t.kc}x</span> ${MONSTERS[t.mt]||'#'+t.mt}`).join(', ');
    html+=`<tr class="${mod}" id="qrow-${q.id}">`;
    html+=`<td class="cell-id">${q.id}</td>`;
    html+=`<td class="cell-name" onclick="openPanel(${q.id})">${q.name}</td>`;
    html+=`<td class="cell-targets">${tgts||'—'}</td>`;
    html+=`<td class="cell-loc">${q.loc}</td>`;
    for(let c=0;c<4;c++){
      html+='<td class="reward-cell">';
      const maxSlot=getMaxSlot(q.id,c);
      for(let s=0;s<=maxSlot;s++)html+=rwSlotHtml(q.id,c,s);
      if(maxSlot<3)html+=`<div class="rw-add" onclick="addSlot(${q.id},${c})">+ add slot</div>`;
      html+='</td>';
    }
    html+='</tr>';
  }
  html+='</tbody></table>';
  content.innerHTML=html;
}

function getMaxSlot(qid,ci){
  let max=-1;
  if(rewards[qid]?.[ci])for(const s of Object.keys(rewards[qid][ci]))max=Math.max(max,parseInt(s));
  return Math.max(max,1); // at least slots 0 and 1
}

function rwSlotHtml(qid,ci,rs){
  const rw=rewards[qid]?.[ci]?.[rs];
  const mod=isSlotMod(qid,ci,rs)?'modified':'';
  if(rw&&rw.def>0){
    const it=items.find(i=>i.def===rw.def);
    const nm=it?it.name:'#'+rw.def;
    return `<div class="rw-slot ${mod}">
      <span class="rw-name" title="${nm}" onclick="openModal(${qid},${ci},${rs})">${nm}</span>
      <div class="rw-lvl-area">
        <button class="lvl-btn" onclick="adjLvl(${qid},${ci},${rs},-1)">-</button>
        <span class="rw-lvl">+${rw.lvl}</span>
        <button class="lvl-btn" onclick="adjLvl(${qid},${ci},${rs},1)">+</button>
      </div>
      <button class="rw-rm" onclick="rmSlot(${qid},${ci},${rs})" title="Remove">&times;</button>
    </div>`;
  }
  return `<div class="rw-slot ${mod}" onclick="openModal(${qid},${ci},${rs})"><span class="rw-empty">empty</span>
    ${rs>1?`<button class="rw-rm" onclick="event.stopPropagation();rmSlot(${qid},${ci},${rs})" title="Remove">&times;</button>`:''}
  </div>`;
}

window.addSlot=function(qid,ci){
  const max=getMaxSlot(qid,ci);
  const ns=max+1;
  if(ns>3)return;
  setRw(qid,ci,ns,0,0);
  renderTable();updateMod();
};

window.rmSlot=function(qid,ci,rs){
  if(!rewards[qid]?.[ci])return;
  delete rewards[qid][ci][rs];
  // Reindex: shift higher slots down
  const slots=rewards[qid][ci];
  const sorted=Object.keys(slots).map(Number).sort((a,b)=>a-b);
  const newSlots={};
  sorted.forEach((k,i)=>{newSlots[i]=slots[k];});
  rewards[qid][ci]=newSlots;
  refreshRow(qid);updateMod();
};

// ── Level adjust ──
window.adjLvl=function(qid,ci,rs,d){
  const rw=rewards[qid]?.[ci]?.[rs];if(!rw)return;
  const nl=Math.max(0,Math.min(15,rw.lvl+d));if(nl===rw.lvl)return;
  setRw(qid,ci,rs,rw.def,nl);refreshRow(qid);updateMod();
};

// ── Detail panel ──
window.openPanel=function(qid){
  editingQid=qid;
  const q=quests.find(x=>x.id===qid);if(!q)return;
  $('dp-title').textContent='Quest #'+qid;
  $('dp-name').value=q.name;$('dp-location').value=q.loc;$('dp-lore').value=q.lore;
  const tc=$('dp-targets');tc.innerHTML='';
  const tgts=targets[qid]||[];
  for(const t of tgts)addTargetRow(t.mt,t.kc);
  if(!tgts.length)addTargetRow(0,10);
  panel.classList.remove('hidden');
};
function closePanel(){panel.classList.add('hidden');editingQid=null;}

function addTargetRow(mt=0,kc=10){
  const c=$('dp-targets'),row=document.createElement('div');row.className='target-row';
  let opts='';for(const[t,n]of Object.entries(MONSTERS).sort((a,b)=>a[0]-b[0]))opts+=`<option value="${t}"${parseInt(t)===mt?' selected':''}>[${t}] ${n}</option>`;
  row.innerHTML=`<select>${opts}</select><input type="number" min="1" max="255" value="${kc}"><span style="color:var(--text3);font-size:11px">kills</span><button class="btn-close" onclick="this.parentElement.remove()">&times;</button>`;
  c.appendChild(row);
}

function applyDetail(){
  if(editingQid===null)return;
  const q=quests.find(x=>x.id===editingQid);if(!q)return;
  q.name=$('dp-name').value.trim()||q.name;
  q.loc=$('dp-location').value.trim()||q.loc;
  q.lore=$('dp-lore').value;
  const rows=$('dp-targets').querySelectorAll('.target-row');
  const nt=[];rows.forEach((r,i)=>{nt.push({ti:i,mt:parseInt(r.querySelector('select').value),kc:parseInt(r.querySelector('input').value)||1});});
  targets[editingQid]=nt;
  try{
    db.run("UPDATE quest_definitions SET quest_name=?,location=?,lore_text=? WHERE quest_id=?",[q.name,q.loc,q.lore,editingQid]);
    db.run("DELETE FROM quest_targets WHERE quest_id=?",[editingQid]);
    const s=db.prepare("INSERT INTO quest_targets (quest_id,target_index,monster_type,kills_required) VALUES (?,?,?,?)");
    for(const t of nt)s.run([editingQid,t.ti,t.mt,t.kc]);s.free();
  }catch(e){console.error(e);}
  closePanel();renderNav();renderTable();updateMod();
}

// ── Item modal ──
window.openModal=function(qid,ci,rs){
  pendingSlot={qid,ci,rs};
  const rw=rewards[qid]?.[ci]?.[rs];
  levelInput.value=rw?rw.lvl:0;
  searchInput.value='';
  document.querySelectorAll('.fbtn').forEach(b=>b.classList.remove('active'));
  document.querySelector('.fbtn[data-cat="all"]').classList.add('active');
  renderItemList(rw?rw.def:null);
  modal.classList.remove('hidden');searchInput.focus();
};
function closeModal(){modal.classList.add('hidden');pendingSlot=null;}

function renderItemList(selDef){
  const q=searchInput.value.toLowerCase().trim();
  const cat=document.querySelector('.fbtn.active')?.dataset.cat;
  itemList.innerHTML='';
  let f=items;
  if(cat&&cat!=='all')f=f.filter(i=>i.cat===parseInt(cat));
  if(q)f=f.filter(i=>i.name.toLowerCase().includes(q));
  for(const it of f.slice(0,150)){
    const row=document.createElement('div');
    row.className='irow'+(selDef===it.def?' selected':'');
    row.innerHTML=`<span class="irow-cat">${CAT_NAMES[it.cat]||'Cat'+it.cat}</span><span class="irow-name">${it.name}</span><span class="irow-def">[${it.cat}:${it.idx}]</span>`;
    row.onclick=()=>pickItem(it);
    itemList.appendChild(row);
  }
  if(f.length>150){const m=document.createElement('div');m.className='irow';m.innerHTML=`<span class="irow-name" style="color:var(--text3)">${f.length-150} more...</span>`;itemList.appendChild(m);}
  if(!f.length){const m=document.createElement('div');m.className='irow';m.innerHTML='<span class="irow-name" style="color:var(--text3)">No items found</span>';itemList.appendChild(m);}
}

function pickItem(it){
  if(!pendingSlot)return;
  const{qid,ci,rs}=pendingSlot;
  setRw(qid,ci,rs,it.def,parseInt(levelInput.value)||0);
  closeModal();refreshRow(qid);updateMod();
}
function onClear(){
  if(!pendingSlot)return;
  const{qid,ci,rs}=pendingSlot;
  setRw(qid,ci,rs,0,0);closeModal();refreshRow(qid);updateMod();
}

// ── Helpers ──
function setRw(qid,ci,rs,def,lvl){if(!rewards[qid])rewards[qid]={};if(!rewards[qid][ci])rewards[qid][ci]={};rewards[qid][ci][rs]={def,lvl};}

function isSlotMod(qid,ci,rs){
  const c=rewards[qid]?.[ci]?.[rs],o=originalData.rewards?.[qid]?.[ci]?.[rs];
  if(!c&&!o)return false;if(!c||!o)return true;return c.def!==o.def||c.lvl!==o.lvl;
}
function isQuestModified(qid){
  for(let c=0;c<4;c++)for(let s=0;s<2;s++)if(isSlotMod(qid,c,s))return true;
  if(JSON.stringify(targets[qid]||[])!==JSON.stringify(originalData.targets?.[qid]||[]))return true;
  const cq=quests.find(q=>q.id===qid),oq=originalData.quests?.find(q=>q.id===qid);
  if(cq&&oq&&(cq.name!==oq.name||cq.lore!==oq.lore||cq.loc!==oq.loc))return true;
  return false;
}

function refreshRow(qid){
  const old=document.getElementById('qrow-'+qid);
  if(!old)return;
  // Re-render just reward cells
  const cells=old.querySelectorAll('.reward-cell');
  cells.forEach((cell,ci)=>{
    cell.innerHTML='';
    for(let s=0;s<2;s++)cell.innerHTML+=rwSlotHtml(qid,ci,s);
  });
  old.className=isQuestModified(qid)?'modified':'';
}

function updateMod(){
  const any=quests.some(q=>isQuestModified(q.id));
  status(any?'Modified':'No changes',any?'modified':'saved');
}

// ── Save ──
async function onSave(){
  if(!db)return;
  try{
    db.run("DELETE FROM quest_rewards");
    const s=db.prepare("INSERT INTO quest_rewards (quest_id,class_index,reward_slot,def_index,item_level) VALUES (?,?,?,?,?)");
    for(const[q,cs]of Object.entries(rewards))for(const[c,ss]of Object.entries(cs))for(const[sl,r]of Object.entries(ss))s.run([+q,+c,+sl,r.def,r.lvl]);
    s.free();
    db.run("DELETE FROM quest_targets");
    const t=db.prepare("INSERT INTO quest_targets (quest_id,target_index,monster_type,kills_required) VALUES (?,?,?,?)");
    for(const[q,ts]of Object.entries(targets))for(const x of ts)t.run([+q,x.ti,x.mt,x.kc]);
    t.free();
    const u=db.prepare("UPDATE quest_definitions SET quest_name=?,location=?,lore_text=? WHERE quest_id=?");
    for(const q of quests)u.run([q.name,q.loc,q.lore,q.id]);
    u.free();
  }catch(e){alert('Save error: '+e.message);return;}
  const data=db.export();
  const r=await fetch('/api/db',{method:'POST',headers:{'Content-Type':'application/octet-stream'},body:data.buffer});
  if(r.ok){
    originalData={rewards:JSON.parse(JSON.stringify(rewards)),targets:JSON.parse(JSON.stringify(targets)),quests:JSON.parse(JSON.stringify(quests))};
    status('Saved','saved');renderTable();
  }else alert('Server error: '+r.status);
}
