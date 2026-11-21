const EDGE_LIMIT = 4096;
const FILE_LIMIT = 32 * 1024 * 1024;

async function readSize(file) {
 if (!file || file.size < 33 || file.size > FILE_LIMIT) throw Error('请选择不超过 32 MiB 的 PNG。');
 const header = new DataView(await file.slice(0, 24).arrayBuffer());
 if (header.getUint32(0) !== 0x89504e47 || header.getUint32(4) !== 0x0d0a1a0a ||
     header.getUint32(8) !== 13 || header.getUint32(12) !== 0x49484452) {
  throw Error('请选择有效的 PNG。');
 }
 const width = header.getUint32(16), height = header.getUint32(20);
 if (!width || !height || Math.max(width, height) > EDGE_LIMIT) throw Error('PNG 边长须在 1 到 4096 像素之间。');
 return {width, height};
}

async function draw(file, canvas) {
 const bitmap = await createImageBitmap(file);
 try {
  const context = canvas.getContext('2d', {willReadFrequently: true});
  context.imageSmoothingQuality = 'high';
  context.drawImage(bitmap, 0, 0, canvas.width, canvas.height);
  return context;
 } finally {
  bitmap.close();
 }
}

function node(tag, text, className) {
 const element = document.createElement(tag);
 if (text) element.textContent = text;
 if (className) element.className = className;
 return element;
}

export function imagePreview() {
 const panel = node('details', '', 'manager-card');
 const source = node('input'), external = node('input'), scale = node('select');
 for (const input of [source, external]) {
  input.type = 'file';
  input.accept = 'image/png,.png';
 }
 scale.append(new Option('2 倍 · 1440p', '2'), new Option('3 倍 · 2160p', '3'));
 const fields = node('div', '', 'settings-grid');
 for (const [label, control] of [['原图 PNG', source], ['预览倍率', scale], ['自备 AI 输出 PNG（可选）', external]]) {
  const field = node('label', label, 'settings-field');
  field.append(control);
  fields.append(field);
 }
 const generate = node('button', '生成本地预览', 'ui-btn is-primary');
 const save = node('button', '保存 PNG', 'ui-btn is-ghost');
 const status = node('p', '', 'muted');
 status.setAttribute('role', 'status');
 const comparison = node('div', '', 'settings-grid');
 const actions = node('div', '', 'actions');
 actions.append(generate, save);
 panel.append(node('summary', '本地图片高清预览'), node('p',
  '放大自备 PNG，或比较本地 AI 的成图。生成的 PNG 保存在你的电脑上，可用于支持该尺寸的素材导入入口。', 'muted'),
  fields, actions, status, comparison);
 let revision = 0, output = null, busy = false;
 function reset() {
  ++revision;
  output = null;
  comparison.replaceChildren();
  status.textContent = '';
  save.disabled = true;
  generate.disabled = busy || !source.files.length;
 }
 for (const input of [source, scale, external]) input.addEventListener('change', reset);
 reset();
 generate.onclick = async () => {
  reset();
  const requestRevision = revision;
  const input = source.files[0], enhancement = external.files[0], factor = Number(scale.value);
  generate.disabled = busy = true;
  status.textContent = '正在本地处理…';
  try {
   const size = await readSize(input);
   const width = size.width * factor, height = size.height * factor;
   if (Math.max(width, height) > EDGE_LIMIT) throw Error('放大后的图片边长不得超过 4096 像素。');
   const original = node('canvas');
   original.width = width;
   original.height = height;
   const originalPixels = await draw(input, original);
   let preview = original;
   if (enhancement) {
    const resultSize = await readSize(enhancement);
    if (resultSize.width !== width || resultSize.height !== height) {
     throw Error(`AI 成图尺寸应为 ${width} × ${height}。`);
    }
    preview = node('canvas');
    preview.width = width;
    preview.height = height;
    const context = await draw(enhancement, preview);
    const result = context.getImageData(0, 0, width, height);
    const baseline = originalPixels.getImageData(0, 0, width, height);
    for (let index = 3; index < result.data.length; index += 4) {
     if (result.data[index] === 0 && baseline.data[index] !== 0)
      result.data.set(baseline.data.subarray(index - 3, index), index - 3);
     result.data[index] = baseline.data[index];
    }
    context.putImageData(result, 0, 0);
   }
   const png = await new Promise(resolve => preview.toBlob(resolve, 'image/png'));
   if (!png) throw Error('PNG 编码失败。');
   if (requestRevision !== revision) return;
   if (enhancement) {
    const baseline = node('figure', '', 'image-preview');
    baseline.append(original, node('figcaption', '常规放大'));
    comparison.append(baseline);
   }
   const result = node('figure', '', 'image-preview');
   result.append(preview, node('figcaption', enhancement ? 'AI 成图 · 原图透明度' : '常规放大'));
   comparison.append(result);
   output = {png, name: input.name.replace(/\.png$/i, '') + '-' + factor + 'x.png'};
   save.disabled = false;
   status.textContent = `${size.width} × ${size.height} → ${width} × ${height}`;
  } catch (error) {
   if (requestRevision === revision) status.textContent = error.message;
  } finally {
   busy = false;
   generate.disabled = !source.files.length;
  }
 };
 save.onclick = () => {
  if (!output) return;
  const link = node('a');
  link.href = URL.createObjectURL(output.png);
  link.download = output.name;
  link.click();
  setTimeout(() => URL.revokeObjectURL(link.href), 1000);
 };
 return panel;
}
