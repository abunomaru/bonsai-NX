// Renders Bonsai's package images (dist/uwp/AssetsBonsai) from docs/bonsai/*.svg with the
// Inter font the app ships. Run from the repository root: node tools/bonsai/render_assets.js
// (needs Playwright with Chromium). package-appx.ps1 -Nx puts these over the Jadyx images.
const { chromium } = require('playwright');
const fs = require('fs');
const path = require('path');

const root = process.cwd();
const out = path.join(root, 'dist/uwp/AssetsBonsai');
const font = 'file://' + path.join(root, 'dist/uwp/Assets/Fonts/Inter-SemiBold.ttf');
const inner = (file) => fs.readFileSync(path.join(root, 'docs/bonsai', file), 'utf8')
    .replace(/^[\s\S]*?<svg[^>]*>/, '').replace(/<\/svg>\s*$/, '');
const markDark = inner('bonsai-mark-on-dark.svg');
const iconDark = inner('bonsai-icon-dark.svg');
const svg = (body, size = 512) => `<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 512 512" width="${size}" height="${size}">${body}</svg>`;
const tileBg = 'linear-gradient(135deg,#1B2A24,#0E1714)';
const word = (px, color) => `<div style="font-family:Inter;font-weight:600;font-size:${px}px;letter-spacing:${px * 0.42}px;color:${color};margin-right:-${px * 0.42}px">BONSAI</div>`;

const jobs = [
  // Full-bleed tiles: the console rounds or frames them itself.
  ['Square150x150Logo.scale-200.png', 300, 300, `<div style="width:300px;height:300px;background:${tileBg};display:flex;align-items:center;justify-content:center">${svg(markDark, 250)}</div>`],
  ['Square44x44Logo.scale-200.png', 88, 88, `<div style="width:88px;height:88px">${svg(iconDark.replace('rx="116"', 'rx="0"'), 88)}</div>`],
  ['StoreLogo.scale-200.png', 100, 100, `<div style="width:100px;height:100px">${svg(iconDark.replace('rx="116"', 'rx="0"'), 100)}</div>`],
  ['Wide310x150Logo.scale-200.png', 620, 300, `<div style="width:620px;height:300px;background:${tileBg};display:flex;align-items:center;justify-content:center;gap:18px">${svg(markDark, 230)}${word(40, '#EDE4D0')}</div>`],
  // Shown on the manifest's black splash background: transparent around the mark.
  ['SplashScreen.scale-200.png', 1240, 600, `<div style="width:1240px;height:600px;display:flex;align-items:center;justify-content:center">${svg(markDark, 420)}</div>`],
  // The launch card's stacked logo (over a dark gradient), and the in-app logo and wordmark.
  ['LaunchLogo.png', 1850, 1394, `<div style="width:1850px;height:1394px;display:flex;flex-direction:column;align-items:center;justify-content:center;gap:10px">${svg(markDark, 1060)}${word(150, '#EDE4D0')}</div>`],
  ['JadyxLogo.png', 512, 512, `<div style="width:512px;height:512px">${svg(markDark, 512)}</div>`],
  ['JadyxWordmark.png', 1200, 300, `<div style="width:1200px;height:300px;display:flex;align-items:center;justify-content:center">${word(150, '#EDE4D0')}</div>`],
];

(async () => {
  fs.mkdirSync(out, { recursive: true });
  const browser = await chromium.launch();
  for (const [name, w, h, body] of jobs) {
    const page = await browser.newPage({ viewport: { width: w, height: h }, deviceScaleFactor: 1 });
    await page.setContent(`<html><head><style>@font-face{font-family:Inter;font-weight:600;src:url('${font}')}html,body{margin:0;background:transparent}</style></head><body>${body}</body></html>`);
    await page.evaluate(() => document.fonts.ready);
    await page.screenshot({ path: path.join(out, name), omitBackground: true, clip: { x: 0, y: 0, width: w, height: h } });
    await page.close();
    console.log(name, w + 'x' + h);
  }
  await browser.close();
})();
