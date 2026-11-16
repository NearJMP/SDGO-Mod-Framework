import {e,button} from '/panel_helpers.js';

const ROLE_NAMES={world:'世界服务器',battle:'对战服务器',agent:'中继服务器'};
const FORMAT_NAMES={png:'PNG',jpeg:'JPEG',bmp:'BMP',gif:'GIF',webp:'WebP',tiff:'TIFF',tga:'TGA',dds:'DDS',ico:'ICO',avif:'AVIF',heic:'HEIC / HEIF',svg:'SVG'};

function option(value,text){const node=e('option',text);node.value=value;return node;}
function positive(value,label){const number=Number(value);if(!Number.isInteger(number)||number<1||number>8192)throw Error(label+'必须是 1–8192 的整数');return number;}
function placement(value,label){const number=Number(value);if(!Number.isFinite(number)||number<0.2||number>8)throw Error(label+'必须是 0.2–8.0 的数字');return number;}
function base64(file){return new Promise((resolve,reject)=>{const reader=new FileReader();reader.onerror=()=>reject(Error('读取图片失败'));reader.onload=()=>resolve(String(reader.result).split(',',2)[1]||'');reader.readAsDataURL(file);});}

export async function mount(root,ctx){
 root.classList.add('sticker-panel');
 const style=e('style');style.textContent=`
 .sticker-panel{--sticker-line:var(--ui-line-strong,#334054)}
 .sticker-panel .sticker-head{display:flex;justify-content:space-between;gap:18px;align-items:flex-start;margin-bottom:18px}
 .sticker-panel .sticker-head h2{margin-bottom:6px}.sticker-panel .sticker-head p{margin:0}
 .sticker-panel .workspace{display:grid;grid-template-columns:260px minmax(0,1fr);gap:18px;align-items:start}
 .sticker-panel .project-list{display:grid;gap:7px;max-height:720px;overflow:auto}
 .sticker-panel .project-row{width:100%;display:grid;grid-template-columns:44px 1fr;gap:10px;align-items:center;text-align:left;padding:9px;border:0;border-radius:8px;background:var(--ui-hull-850,#151c28);color:var(--ui-text,#eaf0fa);box-shadow:inset 2px 0 0 var(--ui-line,#28384d);cursor:pointer}
 .sticker-panel .project-row:hover,.sticker-panel .project-row.is-active{background:#172b3d;box-shadow:inset 3px 0 0 var(--ui-cyan,#69dce8)}
 .sticker-panel .project-thumb{width:44px;height:44px;display:grid;place-items:center;border-radius:7px;background:linear-gradient(45deg,#243040 25%,transparent 25% 75%,#243040 75%),linear-gradient(45deg,#243040 25%,#182231 25% 75%,#243040 75%);background-position:0 0,6px 6px;background-size:12px 12px;color:var(--ui-muted,#9fadc2)}
 .sticker-panel .project-row strong,.sticker-panel .project-row small{display:block;overflow:hidden;text-overflow:ellipsis;white-space:nowrap}.sticker-panel .project-row small{color:var(--ui-muted,#9fadc2)}
 .sticker-panel .empty-project{padding:36px 16px;text-align:center;color:var(--ui-muted,#9fadc2)}
 .sticker-panel .editor-grid{display:grid;grid-template-columns:minmax(300px,1fr) minmax(300px,1fr);gap:18px}
 .sticker-panel .form-grid{display:grid;grid-template-columns:1fr 1fr;gap:13px}.sticker-panel .form-grid .wide{grid-column:1/-1}
 .sticker-panel label{display:grid;gap:6px}.sticker-panel textarea{min-height:96px;resize:vertical}
 .sticker-panel .preview-stage{min-height:360px;display:grid;place-items:center;padding:24px;border-radius:12px;background-color:#111927;background-image:linear-gradient(45deg,#1f2b3c 25%,transparent 25%),linear-gradient(-45deg,#1f2b3c 25%,transparent 25%),linear-gradient(45deg,transparent 75%,#1f2b3c 75%),linear-gradient(-45deg,transparent 75%,#1f2b3c 75%);background-size:22px 22px;background-position:0 0,0 11px,11px -11px,-11px 0}
 .sticker-panel .shield{width:min(78%,380px);aspect-ratio:1/1.08;display:grid;place-items:center;padding:17%;clip-path:polygon(50% 0,92% 14%,88% 66%,50% 100%,12% 66%,8% 14%);background:linear-gradient(145deg,#5e7187,#26384d 56%,#172333);filter:drop-shadow(0 18px 18px rgb(0 0 0 / 35%));box-sizing:border-box}
 .sticker-panel .preview-frame{display:grid;place-items:center;overflow:hidden;max-width:100%;max-height:100%;border:1px solid rgb(207 228 246 / 32%);box-sizing:border-box}.sticker-panel .preview-frame img,.sticker-panel .preview-frame span{grid-area:1/1;min-width:0;min-height:0}.sticker-panel .shield img{width:100%;height:100%;object-fit:contain;filter:drop-shadow(0 4px 5px rgb(0 0 0 / 28%))}.sticker-panel .shield span{color:#c8d5e5;text-align:center}
 .sticker-panel .preview-label{text-align:center;margin:10px 0 0;color:var(--ui-muted,#9fadc2);font-size:12px}
 .sticker-panel .size-row{display:grid;grid-template-columns:1fr auto 1fr;gap:8px;align-items:end}.sticker-panel .size-row span{padding-bottom:10px;color:var(--ui-muted,#9fadc2)}
 .sticker-panel .inline-check{display:flex;align-items:center;gap:8px}.sticker-panel .inline-check input{width:auto}
 .sticker-panel .placement-grid{display:grid;grid-template-columns:1fr 1fr;gap:12px}.sticker-panel .placement-pair{display:grid;grid-template-columns:1fr 82px;gap:8px;align-items:center}.sticker-panel .placement-pair input[type=range]{padding:0}.sticker-panel .native-output{margin-top:14px;padding:12px;border:1px solid var(--ui-line,#28384d);border-radius:8px;background:#101824}.sticker-panel .native-output pre{max-height:300px;overflow:auto;white-space:pre-wrap;overflow-wrap:anywhere;margin:8px 0 0;font-size:12px}.sticker-panel .native-output[hidden]{display:none}
 .sticker-panel .button-row{display:flex;gap:10px;flex-wrap:wrap;margin-top:16px}.sticker-panel .danger{color:#ff9ba4;border-color:#8d3a43}
 .sticker-panel .preset{display:grid;grid-template-columns:96px 1fr auto;gap:16px;align-items:center}.sticker-panel .preset img{width:96px;height:112px;object-fit:contain;border-radius:9px;background:linear-gradient(45deg,#202b3b 25%,transparent 25% 75%,#202b3b 75%),linear-gradient(45deg,#202b3b 25%,#121a26 25% 75%,#202b3b 75%);background-position:0 0,7px 7px;background-size:14px 14px}
 .sticker-panel .format-grid{display:flex;gap:7px;flex-wrap:wrap}.sticker-panel .format-chip{padding:5px 9px;border-radius:999px;background:#173023;color:#91e9b6;font-size:12px}.sticker-panel .format-chip.off{background:#382429;color:#ffadb4}
 .sticker-panel .native-status{display:flex;gap:12px;align-items:flex-start}.sticker-panel .status-dot{width:11px;height:11px;margin-top:5px;border-radius:50%;background:#f3c65e;box-shadow:0 0 12px rgb(243 198 94 / 50%)}
 @media(max-width:1000px){.sticker-panel .workspace,.sticker-panel .editor-grid{grid-template-columns:1fr}.sticker-panel .project-list{max-height:260px}.sticker-panel .preset{grid-template-columns:80px 1fr}.sticker-panel .preset .ui-btn{grid-column:1/-1}.sticker-panel .form-grid{grid-template-columns:1fr}}
 `;root.append(style);

 let state=await ctx.request('/api/stickers',undefined,'GET');
 let project=null,saved='',imageUrl=null,nativePackageUrl=null,updatingSize=false;
 const preset=state.presets[0];

 const intro=e('section',undefined,'manager-card'),head=e('div',undefined,'sticker-head');
 head.append(e('div'),button('新建空白项目',()=>createProject(null),ctx,'ui-btn is-blue'));
 head.firstChild.append(e('h2','贴纸项目'),e('p','导入图片、设置展示尺寸并选择现有客户端与服务器。项目先保存在 Manager 中。','muted'));
 intro.append(head);
 const presetBox=e('div',undefined,'preset'),presetImage=document.createElement('img');presetImage.src=preset.asset_url;presetImage.alt='大肥鱼公开预设';
 const presetCopy=e('div');presetCopy.append(e('strong',preset.display_name),e('p',preset.description,'muted'),e('small',`${preset.width} × ${preset.height} · 透明背景`,'muted'));
 presetBox.append(presetImage,presetCopy,button('使用此预设',()=>createProject(preset.id),ctx,'ui-btn is-primary'));intro.append(presetBox);root.append(intro);

 const work=e('section',undefined,'workspace'),sidebar=e('section',undefined,'manager-card'),list=e('div',undefined,'project-list');sidebar.append(e('h2','已保存项目'),list);
 const editor=e('div');work.append(sidebar,editor);root.append(work);

 const capabilityCard=e('section',undefined,'manager-card'),formats=e('div',undefined,'format-grid');
 const available=[],blocked=[];for(const [id,item] of Object.entries(state.codecs.formats)){(item.available?available:blocked).push(FORMAT_NAMES[id]||id.toUpperCase());formats.append(e('span',FORMAT_NAMES[id]||id.toUpperCase(),'format-chip'+(item.available?'':' off')));}
 capabilityCard.append(e('h2','图片格式'),e('p','可用：'+available.join('、')+'。导入后统一保存为保留透明通道的 PNG。','muted'),formats);
 if(blocked.length)capabilityCard.append(e('p','当前未启用：'+blocked.join('、')+'。页面会显示具体依赖或安全限制。','muted'));
 root.append(capabilityCard);

 function hostSelect(items,placeholder){const select=document.createElement('select');select.append(option('',placeholder));for(const item of items)select.append(option(item.id,item.name+(item.role_name?' · '+item.role_name:'')));return select;}
 function presentationFrom(width,height){const w=width.value.trim(),h=height.value.trim();return {size:(!w&&!h)?null:{width:positive(w,'宽度'),height:positive(h,'高度')},fit:'contain',padding_rgba:[0,0,0,0]};}
 function currentDraft(){if(!project)return null;return {display_name:name.input.value,description:description.input.value,hosts:{client:client.value||null,server:server.value||null},presentation:presentationFrom(width.input,height.input),settings:{...(project.settings||{}),default_width:placement(defaultWidth.number.value,'首次放置宽度'),default_height:placement(defaultHeight.number.value,'首次放置高度')}};}
 function serializedDraft(){try{return JSON.stringify(currentDraft());}catch{return '__invalid__';}}
 function serverDraft(value){return JSON.stringify({display_name:value.display_name,description:value.description,hosts:value.hosts,presentation:value.presentation,settings:value.settings||{}});}
 function dirty(){return !!project&&serializedDraft()!==saved;}
 ctx.guard?.(()=>dirty()?'「自定义贴纸」中有未保存的项目信息。':'');
 function confirmSwitch(){return !dirty()||confirm('当前项目有未保存的名称、说明、目标或尺寸。仍要离开吗？');}

 const name={input:document.createElement('input')},description={input:document.createElement('textarea')},client=hostSelect(state.hosts.clients,'暂不选择客户端'),server=hostSelect(state.hosts.servers,'暂不选择服务器');
 const width={input:document.createElement('input')},height={input:document.createElement('input')};for(const input of [width.input,height.input]){input.type='number';input.min='1';input.max='8192';input.step='1';}
 const lock=document.createElement('input');lock.type='checkbox';lock.checked=true;
 const file=document.createElement('input');file.type='file';file.accept=Object.values(state.codecs.formats).filter(item=>item.available).flatMap(item=>item.extensions).join(',');
 const firstFrame=document.createElement('input');firstFrame.type='checkbox';
 function placementPair(){const range=document.createElement('input'),number=document.createElement('input');range.type='range';number.type='number';for(const input of [range,number]){input.min='0.2';input.max='8';input.step='0.1';}range.addEventListener('input',()=>{number.value=range.value;});number.addEventListener('input',()=>{if(number.value)range.value=number.value;});return {range,number};}
 const defaultWidth=placementPair(),defaultHeight=placementPair();
 const shield=e('div',undefined,'shield'),preview=document.createElement('img'),previewEmpty=e('span','导入图片后在这里预览'),previewFrame=e('div',undefined,'preview-frame');previewFrame.append(preview,previewEmpty);shield.append(previewFrame);preview.hidden=true;
 const imageInfo=e('p','尚未导入图片','muted');

 function setSelect(select,value,label){select.value=value||'';if(value&&!select.value){select.append(option(value,label+'（已从 Manager 移除）'));select.value=value;}}
 function aspect(){if(project?.image)return project.image.width/project.image.height;return 1;}
 function updatePreviewBounds(){const w=Number(width.input.value),h=Number(height.input.value);let ratio=Number.isFinite(w)&&Number.isFinite(h)&&w>0&&h>0?w/h:aspect();if(!Number.isFinite(ratio)||ratio<=0)ratio=1;previewFrame.style.width='auto';previewFrame.style.height='auto';previewFrame.style.aspectRatio=String(ratio);previewFrame.dataset.aspect=String(ratio);}
 width.input.addEventListener('input',()=>{if(lock.checked&&!updatingSize&&width.input.value){updatingSize=true;height.input.value=Math.max(1,Math.round(Number(width.input.value)/aspect()));updatingSize=false;}updatePreviewBounds();});
 height.input.addEventListener('input',()=>{if(lock.checked&&!updatingSize&&height.input.value){updatingSize=true;width.input.value=Math.max(1,Math.round(Number(height.input.value)*aspect()));updatingSize=false;}updatePreviewBounds();});

 async function showImage(){if(imageUrl){URL.revokeObjectURL(imageUrl);imageUrl=null;}preview.hidden=true;preview.removeAttribute('src');previewEmpty.hidden=false;if(!project?.asset_url)return;const response=await ctx.fetch(project.asset_url);if(!response.ok){const value=await response.json().catch(()=>({}));throw Error(value.error||'读取贴纸图片失败');}imageUrl=URL.createObjectURL(await response.blob());preview.src=imageUrl;preview.hidden=false;previewEmpty.hidden=true;}
 function clearNativeResult(){if(nativePackageUrl){URL.revokeObjectURL(nativePackageUrl);nativePackageUrl=null;}nativeOutput.replaceChildren();nativeOutput.hidden=true;}
 function fill(value,mark=true,draft=null){if(project?.id!==value.id||project?.revision!==value.revision)clearNativeResult();project=value;const source=draft||value;name.input.value=source.display_name;description.input.value=source.description;setSelect(client,source.hosts.client,'原客户端');setSelect(server,source.hosts.server,'原服务器');const size=source.presentation.size;width.input.value=size?.width??'';height.input.value=size?.height??'';updatePreviewBounds();const gameWidth=source.settings?.default_width??2,gameHeight=source.settings?.default_height??2;defaultWidth.number.value=defaultWidth.range.value=gameWidth;defaultHeight.number.value=defaultHeight.range.value=gameHeight;imageInfo.textContent=value.image?`${value.image.width} × ${value.image.height} · ${Math.round(value.image.bytes/1024)} KiB · ${value.source?.format||'PNG'}`:'尚未导入图片';if(mark)saved=serverDraft(value);showImage().catch(error=>ctx.message(error.message,true));renderList();}

 const form=e('section',undefined,'manager-card'),grid=e('div',undefined,'form-grid');
 const nameLabel=e('label','名称','wide');nameLabel.append(name.input);const descLabel=e('label','说明','wide');descLabel.append(description.input);
 const clientLabel=e('label','客户端');clientLabel.append(client);const serverLabel=e('label','服务器');serverLabel.append(server);grid.append(nameLabel,descLabel,clientLabel,serverLabel);
 const sizeLabel=e('div',undefined,'wide'),sizeRow=e('div',undefined,'size-row'),wLabel=e('label','默认宽度'),hLabel=e('label','默认高度');wLabel.append(width.input);hLabel.append(height.input);sizeRow.append(wLabel,e('span','×'),hLabel);const lockLabel=e('label',undefined,'inline-check');lockLabel.append(lock,document.createTextNode('锁定图片宽高比'));sizeLabel.append(e('h3','制作预览尺寸'),e('p','留空时使用图片尺寸。此处用于制作预览；游戏内效果以原生编译预检和实机结果为准。','muted'),sizeRow,lockLabel);grid.append(sizeLabel);const placementBox=e('div',undefined,'wide'),placementGrid=e('div',undefined,'placement-grid'),placementWidthLabel=e('label','宽度'),placementHeightLabel=e('label','高度'),placementWidthPair=e('div',undefined,'placement-pair'),placementHeightPair=e('div',undefined,'placement-pair');placementWidthPair.append(defaultWidth.range,defaultWidth.number);placementHeightPair.append(defaultHeight.range,defaultHeight.number);placementWidthLabel.append(placementWidthPair);placementHeightLabel.append(placementHeightPair);placementGrid.append(placementWidthLabel,placementHeightLabel);placementBox.append(e('h3','首次放置尺寸'),e('p','0.2–8.0；这是游戏内首次放置的逻辑尺寸，与上方制作预览像素分开保存。','muted'),placementGrid);grid.append(placementBox);form.append(e('h2','项目信息'),grid);
 const actions=e('div',undefined,'button-row');actions.append(button('保存项目',saveProject,ctx,'ui-btn is-primary'),button('重新打开',reloadProject,ctx,'ui-btn is-ghost'),button('删除项目',deleteProject,ctx,'ui-btn is-ghost danger'));form.append(actions);

 const imageCard=e('section',undefined,'manager-card'),fileLabel=e('label','导入图片');fileLabel.append(file);const firstLabel=e('label',undefined,'inline-check');firstLabel.append(firstFrame,document.createTextNode('如果 GIF / WebP 是动画，确认只使用第 1 帧'));imageCard.append(e('h2','图片'),fileLabel,firstLabel,imageInfo,button('导入所选图片',importImage,ctx,'ui-btn is-blue'));
 const previewCard=e('section',undefined,'manager-card'),stage=e('div',undefined,'preview-stage');stage.append(shield);previewCard.append(e('h2','制作预览'),stage,e('p','中性盾牌轮廓 · 透明区域以棋盘格显示 · 这不是原生游戏验收','preview-label'));
 const nativeCard=e('section',undefined,'manager-card'),nativeStatus=e('div',undefined,'native-status');nativeStatus.append(e('span',undefined,'status-dot'),e('div',undefined));nativeStatus.lastChild.append(e('strong',state.native.available?'导入功能已就绪':'导入功能尚未就绪'),e('p',state.native.blocker,'muted'));nativeCard.append(e('h2','生成与安装'),nativeStatus);const nativeActions=e('div',undefined,'button-row');for(const [op,label] of [['preflight','原生预检'],['compile','编译资源'],['install','安装到客户端'],['server-plan','生成服务器计划'],['apply-server','应用服务器定义']]){const control=button(label,()=>nativeOperation(op),ctx,'ui-btn is-ghost');control.disabled=!state.native.operations[op.replace('-','_')];nativeActions.append(control);}nativeCard.append(nativeActions);
 const nativeOutput=e('section',undefined,'native-output');nativeOutput.hidden=true;nativeCard.append(nativeOutput);
 const editorGrid=e('div',undefined,'editor-grid');editorGrid.append(form,imageCard,previewCard,nativeCard);editor.append(editorGrid);editor.hidden=true;

 async function refreshList(selectId=null){const value=await ctx.request('/api/stickers/projects/list',{});state={...state,...value};renderList();if(selectId)await loadProject(selectId,true);}
 function renderList(){list.replaceChildren();if(!state.projects.length){list.append(e('div','还没有项目。可以新建空白项目或使用大肥鱼预设。','empty-project'));return;}for(const item of state.projects){const row=e('button',undefined,'project-row'+(project?.id===item.id?' is-active':''));row.type='button';const thumb=e('span',item.ready?'图':'—','project-thumb'),copy=e('span');copy.append(e('strong',item.display_name),e('small',item.ready?(item.image.width+' × '+item.image.height):'等待导入图片'));row.append(thumb,copy);row.onclick=()=>{if(confirmSwitch())loadProject(item.id);};list.append(row);}}
 async function createProject(presetId){if(!confirmSwitch())return;const response=await ctx.request('/api/stickers/projects/create',{display_name:presetId?preset.display_name:'未命名贴纸',description:'',hosts:{client:state.hosts.clients[0]?.id||null,server:state.hosts.servers[0]?.id||null},presentation:{size:null,fit:'contain',padding_rgba:[0,0,0,0]},settings:{default_width:2,default_height:2},preset_id:presetId});state.projects=response.projects;editor.hidden=false;fill(response.project);ctx.message(presetId?'已从大肥鱼预设建立项目。':'已建立空白贴纸项目。');}
 async function loadProject(id,force=false){if(!force&&!confirmSwitch())return;const response=await ctx.request('/api/stickers/projects/get',{project_id:id});editor.hidden=false;fill(response.project);}
 async function saveProject(){if(!project)throw Error('请先选择项目');const draft=currentDraft();const response=await ctx.request('/api/stickers/projects/save',{project_id:project.id,expected_revision:project.revision,...draft});state.projects=response.projects;fill(response.project);ctx.message('贴纸项目已保存。');}
 async function reloadProject(){if(!project)throw Error('请先选择项目');if(dirty()&&!confirm('放弃当前未保存的项目信息并重新打开吗？'))return;await loadProject(project.id,true);ctx.message('已重新打开保存的项目。');}
 async function deleteProject(){if(!project)throw Error('请先选择项目');if(!confirm('删除贴纸项目「'+project.display_name+'」？此操作只删除这个 Manager 项目。'))return;const response=await ctx.request('/api/stickers/projects/delete',{project_id:project.id,expected_revision:project.revision});state.projects=response.projects;project=null;saved='';editor.hidden=true;if(imageUrl){URL.revokeObjectURL(imageUrl);imageUrl=null;}clearNativeResult();renderList();ctx.message('贴纸项目已删除。');}
 async function importImage(){if(!project)throw Error('请先选择项目');const selected=file.files[0];if(!selected)throw Error('请选择图片');if(selected.size>state.codecs.limits.input_bytes)throw Error('图片超过 16 MiB 上限');const draft=currentDraft(),response=await ctx.request('/api/stickers/projects/import',{project_id:project.id,expected_revision:project.revision,filename:selected.name,image_base64:await base64(selected),allow_animated_first_frame:firstFrame.checked,presentation:draft.presentation});state.projects=response.projects;const serverSaved=response.project;fill(serverSaved,true,draft);file.value='';ctx.message(serverSaved.source.discarded_frames?`图片已导入；已按确认只保留第 1 帧，舍弃 ${serverSaved.source.discarded_frames} 帧。`:'图片已导入并保留透明通道。');}
 function renderNativeResult(operation,result){
  const labels={preflight:'原生预检',compile:'编译资源',install:'安装到客户端','server-plan':'服务器计划','apply-server':'应用服务器定义'};
  const value=result&&typeof result==='object'?result:{};
  if(nativePackageUrl){URL.revokeObjectURL(nativePackageUrl);nativePackageUrl=null;}
  nativeOutput.replaceChildren(e('h3',labels[operation]||operation));
  if(value.message)nativeOutput.append(e('p',String(value.message)));
  const summary=[];
  if(Array.isArray(value.files))summary.push('配套资源 '+value.files.length+' 项');
  if(value.server_checked)summary.push('服务器编号检查通过');
  if(value.restart_required)summary.push('正常重启后生效');
  if(summary.length)nativeOutput.append(e('p',summary.join(' · '),'muted'));
  if(value.package_base64&&value.filename){
   const binary=atob(value.package_base64),bytes=new Uint8Array(binary.length);
   for(let index=0;index<binary.length;index++)bytes[index]=binary.charCodeAt(index);
   nativePackageUrl=URL.createObjectURL(new Blob([bytes],{type:'application/zip'}));
   const download=e('a','下载编译包','ui-btn is-blue');download.href=nativePackageUrl;download.download=value.filename;nativeOutput.append(download);
  }
  if(ctx.developer){
   const visible={...value};delete visible.package_base64;delete visible.session_token;
   const details=document.createElement('details'),caption=document.createElement('summary'),receipt=document.createElement('pre');
   caption.textContent='开发者详情';receipt.textContent=JSON.stringify(visible,null,2);details.append(caption,receipt);nativeOutput.append(details);
  }
  nativeOutput.hidden=false;
 }
 async function nativeOperation(operation){if(!project)throw Error('请先选择项目');if(dirty())throw Error('请先保存当前项目，再运行原生操作');const result=await ctx.request('/api/stickers/native/'+operation,{project_id:project.id,expected_revision:project.revision});renderNativeResult(operation,result);ctx.message(result.message||'原生操作完成。');}

 renderList();if(state.projects.length)await loadProject(state.projects[0].id,true);
 return()=>{if(imageUrl)URL.revokeObjectURL(imageUrl);if(nativePackageUrl)URL.revokeObjectURL(nativePackageUrl);};
}
