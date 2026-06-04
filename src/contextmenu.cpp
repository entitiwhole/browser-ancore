#include "contextmenu.h"
#include "mainwindow.h"
#include "browsercore.h"
#include "saveas.h"
#include <WebView2.h>
#include <wrl.h>
#include <fstream>
#include <sstream>
#include <vector>
#include <shlwapi.h>

#pragma comment(lib, "shlwapi.lib")

#pragma comment(lib, "urlmon.lib")

using Microsoft::WRL::Callback;

static const wchar_t kCtxMenuInline[] = LR"ANC(
(function(){
if(window.__ancoreShowCtxMenu)return;
window.__ancoreCtxInit=true;
var css=document.createElement('style');
css.textContent='#ancore-ctx-root{position:fixed;inset:0;z-index:2147483647;display:none;font:12px/1.4 "Segoe UI",-apple-system,sans-serif}#ancore-ctx-root.open{display:block}#ancore-ctx-menu{position:fixed;min-width:228px;max-width:340px;background:#1A1A1A;color:#E5E2E1;border:1px solid #333;border-radius:12px;padding:6px 0;box-shadow:0 12px 36px rgba(0,0,0,.62),0 0 0 1px rgba(124,77,255,.1);user-select:none;overflow:hidden}#ancore-ctx-menu .ctx-item{padding:9px 16px;cursor:pointer;border-radius:8px;margin:0 6px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis;transition:background .12s ease,color .12s ease}#ancore-ctx-menu .ctx-item:hover:not(.disabled){background:#353535;color:#fff}#ancore-ctx-menu .ctx-item.disabled{color:#666;cursor:default;opacity:.55}#ancore-ctx-menu .ctx-sep{height:1px;background:#333;margin:6px 12px}';
(document.head||document.documentElement).appendChild(css);
var root=document.createElement('div');root.id='ancore-ctx-root';
var menu=document.createElement('div');menu.id='ancore-ctx-menu';
root.appendChild(menu);document.documentElement.appendChild(root);
function hide(){root.classList.remove('open');menu.innerHTML='';}
function post(cmd){try{if(window.__ancorePost)window.__ancorePost(cmd);else{var w=window.chrome&&window.chrome.webview;if(w)w.postMessage(cmd);}}catch(e){}}
window.__ancoreShowCtxMenu=function(x,y,itemsOrJson){var items=itemsOrJson;if(typeof itemsOrJson==='string'){try{items=JSON.parse(itemsOrJson);}catch(e){return;}}if(!items||!items.length)return;menu.innerHTML='';for(var i=0;i<items.length;i++){var it=items[i];if(it.sep){var s=document.createElement('div');s.className='ctx-sep';menu.appendChild(s);continue;}var el=document.createElement('div');el.className='ctx-item'+(it.disabled?' disabled':'');el.textContent=it.label||'';if(!it.disabled&&it.id){(function(id){el.addEventListener('click',function(ev){ev.stopPropagation();hide();post('ctx:'+id);});})(it.id);}menu.appendChild(el);}root.classList.add('open');menu.style.left=x+'px';menu.style.top=y+'px';var r=menu.getBoundingClientRect();var nx=x,ny=y;if(r.right>window.innerWidth-4)nx=Math.max(4,window.innerWidth-r.width-4);if(r.bottom>window.innerHeight-4)ny=Math.max(4,window.innerHeight-r.height-4);menu.style.left=nx+'px';menu.style.top=ny+'px';};
window.__ancoreHideCtxMenu=hide;
root.addEventListener('mousedown',function(e){if(e.target===root)hide();});
document.addEventListener('keydown',function(e){if(e.key==='Escape')hide();},true);
})();
)ANC";

static std::wstring extractJsonString(const std::wstring& json, const wchar_t* key) {
    std::wstring k = std::wstring(L"\"") + key + L"\":\"";
    size_t p = json.find(k);
    if (p == std::wstring::npos) return {};
    p += k.size();
    std::wstring out;
    for (size_t i = p; i < json.size(); i++) {
        if (json[i] == L'\\' && i + 1 < json.size()) {
            wchar_t n = json[++i];
            if (n == L'u' && i + 4 < json.size()) {
                wchar_t hex[5] = {json[i + 1], json[i + 2], json[i + 3], json[i + 4], 0};
                wchar_t ch = (wchar_t)wcstol(hex, nullptr, 16);
                out += ch;
                i += 4;
                continue;
            }
            if (n == L'/' || n == L'\\' || n == L'"') out += n;
            else { out += L'\\'; out += n; }
        } else if (json[i] == L'"') {
            break;
        } else {
            out += json[i];
        }
    }
    return out;
}

namespace {

enum class SaveKind { None, Image, Media, File, Selection };

struct CtxState {
    SaveKind saveKind = SaveKind::None;
    std::wstring saveUrl;
    std::wstring linkUri;
    std::wstring imageUri;
    std::wstring mediaUri;
    std::wstring pageUri;
    COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND targetKind =
        COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_PAGE;
    bool isVideo = false;
    bool imageAtPoint = false;
    bool hitText = false;
    bool hasSelection = false;
    bool viewerOpen = false;
    bool sidebarAction = false;
    BOOL canBack = FALSE;
    BOOL canForward = FALSE;
    int menuX = 0;
    int menuY = 0;
};

CtxState g_ctx;

void registerManualSave(const std::wstring& path) {
    size_t pos = path.rfind(L'\\');
    std::wstring fname = (pos != std::wstring::npos) ? path.substr(pos + 1) : path;
    DownloadInfo di;
    di.fileName = fname;
    di.path = path;
    di.state = 2; // COREWEBVIEW2_DOWNLOAD_STATE_COMPLETED
    di.receivedBytes = di.totalBytes = 0;
    BrowserCore::instance()->addDownload(di);
    BrowserCore::instance()->setStatusText(L"Сохранено: " + fname);
}

std::wstring unquoteScriptResult(LPCWSTR result) {
    if (!result) return {};
    std::wstring s = result;
    if (s.size() >= 2 && s.front() == L'"' && s.back() == L'"')
        s = s.substr(1, s.size() - 2);
    std::wstring out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); i++) {
        if (s[i] == L'\\' && i + 1 < s.size()) {
            wchar_t n = s[++i];
            if (n == L'n') out += L'\n';
            else if (n == L'r') out += L'\r';
            else if (n == L't') out += L'\t';
            else if (n == L'"') out += L'"';
            else if (n == L'\\') out += L'\\';
            else { out += L'\\'; out += n; }
        } else {
            out += s[i];
        }
    }
    return out;
}

void applyLinkImageHints() {
    if (g_ctx.hitText && !g_ctx.imageAtPoint) return;
    if (g_ctx.imageUri.empty() && !g_ctx.linkUri.empty()) {
        std::wstring from = imageUrlFromPageLink(g_ctx.linkUri);
        if (!from.empty()) {
            g_ctx.imageUri = from;
            g_ctx.saveKind = SaveKind::Image;
            g_ctx.saveUrl = from;
            g_ctx.imageAtPoint = true;
        } else if (isImageUrl(g_ctx.linkUri)) {
            g_ctx.imageUri = g_ctx.linkUri;
            g_ctx.saveKind = SaveKind::Image;
            g_ctx.saveUrl = g_ctx.linkUri;
            g_ctx.imageAtPoint = true;
        }
    }
}

void primeDirectImagePageContext() {
    if (!isDirectImagePageUrl(g_ctx.pageUri)) return;
    g_ctx.imageUri = g_ctx.pageUri;
    g_ctx.saveUrl = g_ctx.pageUri;
    g_ctx.imageAtPoint = true;
    g_ctx.saveKind = SaveKind::Image;
    g_ctx.hitText = false;
}

void applyDirectImagePageContext() {
    if (!isDirectImagePageUrl(g_ctx.pageUri) || g_ctx.sidebarAction) return;
    if (g_ctx.hasSelection) return;
    std::wstring url = g_ctx.imageUri.empty() ? g_ctx.pageUri : g_ctx.imageUri;
    if (!isImageUrl(url)) url = g_ctx.pageUri;
    g_ctx.imageUri = url;
    g_ctx.saveUrl = url;
    g_ctx.imageAtPoint = true;
    g_ctx.saveKind = SaveKind::Image;
    g_ctx.hitText = false;
}

void applyYandexPageImageContext() {
    if (!isYandexImageViewerPage(g_ctx.pageUri) || g_ctx.sidebarAction) return;
    std::wstring fromUrl = imageUrlFromPageLink(g_ctx.pageUri);
    if (fromUrl.empty() && g_ctx.imageUri.empty()) return;
    if (fromUrl.empty()) fromUrl = g_ctx.imageUri;
    if (g_ctx.imageUri.empty() || g_ctx.imageUri.rfind(L"blob:", 0) == 0)
        g_ctx.imageUri = fromUrl;
    g_ctx.saveUrl = g_ctx.imageUri;
    if (g_ctx.saveUrl.rfind(L"blob:", 0) == 0)
        g_ctx.saveUrl = fromUrl;
    g_ctx.imageAtPoint = true;
    g_ctx.saveKind = SaveKind::Image;
}

void finalizeMenuContext() {
    if (g_ctx.hasSelection && !g_ctx.imageAtPoint) {
        g_ctx.imageUri.clear();
        g_ctx.saveUrl.clear();
        if (g_ctx.saveKind == SaveKind::Image)
            g_ctx.saveKind = SaveKind::None;
        if (g_ctx.saveKind == SaveKind::None)
            g_ctx.saveKind = SaveKind::Selection;
    }
    if (g_ctx.hitText && !g_ctx.imageAtPoint && !isDirectImagePageUrl(g_ctx.pageUri)) {
        g_ctx.imageUri.clear();
        g_ctx.saveUrl.clear();
        if (g_ctx.saveKind == SaveKind::Image)
            g_ctx.saveKind = SaveKind::None;
    }
    if (g_ctx.imageAtPoint && !g_ctx.imageUri.empty()) {
        g_ctx.saveKind = SaveKind::Image;
        g_ctx.saveUrl = g_ctx.imageUri;
    }
}

static const wchar_t kImgDetectHelpersJs[] =
    LR"JS(
function abs(u){try{if(!u)return '';return new URL(u,document.baseURI||location.href).href}catch(e){return u||''}}
function pick(im){
  if(!im)return '';
  var s=im.currentSrc||im.src||im.getAttribute('data-src')||im.getAttribute('data-iurl')||
    im.getAttribute('data-lazy-src')||im.getAttribute('data-original')||im.getAttribute('data-deferred-src')||
    im.getAttribute('data-url')||im.getAttribute('data-original-src')||'';
  if(!s&&im.srcset){var best='',n=0,p=im.srcset.split(',');
    for(var i=0;i<p.length;i++){var u=p[i].trim().split(/\s+/)[0];var w=parseInt(p[i].trim().split(/\s+/)[1])||0;
      if(u&&(w>=n||!best)){best=u;n=w||n+1;}}s=best;}
  return abs(s);
}
function yandexFromEl(el){
  if(!el)return '';
  var item=el.closest&&(el.closest('.serp-item')||el.closest('[class*=serp-item],[class*=SerpItem]'));
  if(item){
    var bem=item.getAttribute('data-bem');
    if(bem){try{
      var j=JSON.parse(bem);
      var s=j['serp-item']||j.serpItem||j;
      if(s){
        var h=s.img_href||s.origUrl||s.url;
        if(h){
          if(h.indexOf('//')===0)h='https:'+h;
          else if(h.indexOf('http')!==0)h=abs(h);
          return abs(h);
        }
      }
    }catch(e){}}
  }
  var v=el.closest&&el.closest('[class*=MMImage],[class*=ImagesViewer],[class*=MMViewer]');
  if(v){
    var o=v.querySelector&&v.querySelector('img.MMImage-Origin,img.MMImage-Preview,img[class*=MMImage]');
    if(o){var u=pick(o);if(u)return u;}
  }
  return '';
}
function isTextElement(el){
  if(!el||el.nodeType!==1)return false;
  var tn=(el.tagName||'').toLowerCase();
  if(tn==='img'||tn==='video'||tn==='audio'||tn==='canvas'||tn==='svg'||tn==='picture')return false;
  if(tn==='input'){
    var ty=(el.type||'').toLowerCase();
    return ty!=='button'&&ty!=='submit'&&ty!=='checkbox'&&ty!=='radio'&&ty!=='image'&&ty!=='file'&&ty!=='hidden';
  }
  if(tn==='textarea')return true;
  if(tn==='a'||tn==='button'){
    if(el.querySelector&&el.querySelector('img,video,svg,canvas'))return false;
    return !!(el.textContent&&el.textContent.trim());
  }
  var textTags=['p','span','h1','h2','h3','h4','h5','h6','label','li','td','th','dt','dd',
    'figcaption','blockquote','pre','code','em','strong','small','cite','time','b','i','u'];
  if(textTags.indexOf(tn)>=0)return !!(el.textContent&&el.textContent.trim());
  return false;
}
function probeHitKind(list){
  var imgHit=false,textHit=false;
  for(var i=0;i<list.length;i++){
    var n=list[i];
    if(!n||n.nodeType!==1)continue;
    var tn=(n.tagName||'').toUpperCase();
    if(!imgHit&&(tn==='IMG'||tn==='SVG'||tn==='VIDEO'||tn==='CANVAS')){
      var r=n.getBoundingClientRect();
      if(r.width>=8&&r.height>=8)imgHit=true;
    }
    if(!textHit&&isTextElement(n))textHit=true;
    if(imgHit)break;
  }
  return {imgHit:imgHit,textHit:textHit&&!imgHit};
}
function imgFromListAtPoint(x,y,list){
  for(var i=0;i<list.length;i++){
    var n=list[i];if(!n)continue;
    var yx=yandexFromEl(n);if(yx)return yx;
    var im=(n.tagName==='IMG')?n:(n.closest?n.closest('img'):null);
    if(!im&&n.querySelector)im=n.querySelector('img');
    if(im){var u=pick(im);if(u)return u;}
    var bg=n.nodeType===1&&getComputedStyle(n).backgroundImage;
    if(bg&&bg!=='none'){
      var m=bg.match(/url\(['']?([^'')]+)['']?\)/);
      if(m&&m[1])return abs(m[1]);
    }
  }
  return '';
}
function nearestGridImgStrict(x,y){
  var best=null,bestScore=0;
  document.querySelectorAll('img').forEach(function(im){
    var r=im.getBoundingClientRect();
    if(r.width<32||r.height<32)return;
    var hit=r.left<=x+4&&r.right>=x-4&&r.top<=y+4&&r.bottom>=y-4;
    if(!hit)return;
    var u=pick(im);
    if(!u||u.indexOf('data:')===0)return;
    var score=r.width*r.height;
    if(score>bestScore){best=im;bestScore=score;}
  });
  return best?pick(best):'';
}
function yandexHrefFromState(obj){
  if(!obj)return '';
  var h=obj.img_href||obj.origUrl||obj.url||obj.image;
  if(!h)return '';
  if(h.indexOf('//')===0)h='https:'+h;
  else if(h.indexOf('http')!==0)h=abs(h);
  return abs(h);
}
function yandexViewerImage(){
  var nodes=document.querySelectorAll('[id^=ImagesApp][data-state]');
  for(var i=0;i<nodes.length;i++){
    try{
      var j=JSON.parse(nodes[i].getAttribute('data-state'));
      var st=j.initialState||j;
      var vd=st.viewerData||st.viewer||{};
      var h=yandexHrefFromState(vd);
      if(h)return h;
      var cid=st.currentId||st.selectedId||st.openedId;
      var ent=(st.serpList&&st.serpList.items&&st.serpList.items.entities)||{};
      if(cid&&ent[cid]){h=yandexHrefFromState(ent[cid]);if(h)return h;}
    }catch(e){}
  }
  var orig=document.querySelector('img.MMImage-Origin');
  if(orig){var u=pick(orig);if(u&&u.indexOf('blob:')!==0)return u;}
  var prev=document.querySelector('img.MMImage-Preview');
  if(prev){var u2=pick(prev);if(u2)return u2;}
  return '';
}
function yandexModalElement(){
  var nodes=document.querySelectorAll('[class*=ImagesViewer],[class*=MMViewer],[class*=ImageViewer],[class*=Modal],[role=dialog]');
  for(var i=0;i<nodes.length;i++){
    var r=nodes[i].getBoundingClientRect();
    if(r.width>280&&r.height>200)return nodes[i];
  }
  return null;
}
function isYandexSidebarAction(x,y,list){
  var mod=yandexModalElement();
  if(!mod)return false;
  var r=mod.getBoundingClientRect();
  if(x<r.left+r.width*0.68)return false;
  for(var i=0;i<list.length;i++){
    var n=list[i];if(!n||n.nodeType!==1)continue;
    var tn=(n.tagName||'').toLowerCase();
    if(tn==='button')return true;
    if(n.closest&&(n.closest('button,[role=button]')||n.closest('[class*=Button]')))return true;
    if((tn==='a'||tn==='span')&&isTextElement(n))return true;
  }
  return false;
}
function largestImgInRoot(root){
  if(!root)return '';
  var best='',bestA=0;
  root.querySelectorAll('img,canvas').forEach(function(im){
    var r=im.getBoundingClientRect();
    var a=r.width*r.height;
    if(a<4000||a<=bestA)return;
    var u=im.tagName==='CANVAS'?'':pick(im);
    if(im.tagName==='CANVAS'||!u){try{
      if(im.tagName==='CANVAS'&&im.width>0){
        u=im.toDataURL('image/png');}
    }catch(e){}}
    if(u){best=u;bestA=a;}
  });
  return best;
}
function yandexModalImageAt(x,y){
  var mod=yandexModalElement();
  if(!mod)return '';
  var r=mod.getBoundingClientRect();
  if(x<r.left||x>r.right||y<r.top||y>r.bottom)return '';
  var yv=yandexViewerImage();
  if(yv)return yv;
  var u=largestImgInRoot(mod);
  if(u)return u;
  var bg=getComputedStyle(mod).backgroundImage;
  if(bg&&bg!=='none'){
    var m=bg.match(/url\(['']?([^'')]+)['']?\)/);
    if(m&&m[1])return abs(m[1]);
  }
  return '';
}
function yandexViewerBounds(){
  var mod=yandexModalElement();
  if(mod)return mod.getBoundingClientRect();
  var im=document.querySelector('img.MMImage-Origin,img.MMImage-Preview,[class*=MMImage-Origin],[class*=MMImage-Preview],[class*=ImagesViewer] img');
  if(!im)return null;
  var r=im.getBoundingClientRect();
  if(r.width<80||r.height<80)return null;
  return r;
}
function viewerImgAtPoint(x,y){
  var mi=yandexModalImageAt(x,y);
  if(mi)return mi;
  var vb=yandexViewerBounds();
  if(vb&&x>=vb.left-4&&x<=vb.right+4&&y>=vb.top-4&&y<=vb.bottom+4){
    var yv=yandexViewerImage();
    if(yv)return yv;
  }
  return '';
}
function yandexTabPageImage(){
  if(!/rpt=imageview|img_url=|imgurl=/i.test(location.href||''))return '';
  var imgs=document.querySelectorAll('img');
  var best='',bestA=0;
  for(var i=0;i<imgs.length;i++){
    var r=imgs[i].getBoundingClientRect();
    var a=r.width*r.height;
    if(a>bestA){var u=pick(imgs[i]);if(u){best=u;bestA=a;}}
  }
  return best;
}
function isImageSearchPage(){
  var href=location.href||'';
  return /tbm=isch|images\.google|#imgrc|yandex\.(ru|com|kz|by|ua|com\.tr).*\/images|\/images\/(search|touch)|images\.yandex/i.test(href);
}
function isDirectImageDocument(){
  var h=location.href||'';
  if(/^data:image\//i.test(h))return true;
  return /\.(png|jpe?g|gif|webp|bmp|svg|avif|ico)(\?|#|$)/i.test(h);
}
)JS";

static const wchar_t kImageProbeBody[] = LR"JS((function(x,y){
var out={img:'',link:'',sel:'',textHit:'',imgHit:'',viewer:'',sidebar:''};
try{
  var sel=window.getSelection();if(sel&&!sel.isCollapsed)out.sel='1';
  var list=document.elementsFromPoint?document.elementsFromPoint(x,y):[];
  if(!list.length){var e=document.elementFromPoint(x,y);if(e)list=[e];}
  if(yandexModalElement())out.viewer='1';
  if(isYandexSidebarAction(x,y,list))out.sidebar='1';
  var modalImg=yandexModalImageAt(x,y);
  if(modalImg&&!out.sidebar){
    out.img=modalImg;out.imgHit='1';out.textHit='';out.viewer='1';
  }
  if(!out.img){
    var hit=probeHitKind(list);
    if(hit.textHit&&!out.sidebar)out.textHit='1';
    if(hit.imgHit)out.imgHit='1';
    var top=list[0]||document.elementFromPoint(x,y);
    if(top){
      var a=top.closest&&top.closest('a');
      if(a&&a.href)out.link=abs(a.href);
    }
    if(out.textHit!=='1'||out.imgHit==='1'){
      out.img=imgFromListAtPoint(x,y,list);
      if(!out.img&&top){
        for(var n=top;n&&!out.img;n=n.parentElement){
          var y2=yandexFromEl(n);if(y2){out.img=y2;break;}
        }
      }
      if(!out.img)out.img=viewerImgAtPoint(x,y);
      if(!out.img)out.img=yandexTabPageImage();
      if(!out.img&&isImageSearchPage()&&!out.textHit){
        var ng=nearestGridImgStrict(x,y);
        if(ng)out.img=ng;
      }
      if(out.img)out.imgHit='1';
    }
  }
  if(!out.img&&isDirectImageDocument()&&!out.sidebar){
    out.img=abs(location.href);
    out.imgHit='1';out.textHit='';
  }
}catch(e){}
return JSON.stringify(out);
})JS";

static const wchar_t kSavePageImageBody[] = LR"JS((function(x,y){
var img='';
try{
  var list=document.elementsFromPoint?document.elementsFromPoint(x,y):[];
  if(!list.length){var e=document.elementFromPoint(x,y);if(e)list=[e];}
  if(!isYandexSidebarAction(x,y,list))img=yandexModalImageAt(x,y);
  if(!img){
    var hit=probeHitKind(list);
    if(!hit.textHit||hit.imgHit){
      img=imgFromListAtPoint(x,y,list);
      if(!img)img=viewerImgAtPoint(x,y);
      if(!img)img=yandexTabPageImage();
      if(!img&&!hit.textHit&&isImageSearchPage())img=nearestGridImgStrict(x,y);
    }
  }
  if(!img)img=yandexViewerImage();
}catch(e){}
return JSON.stringify({img:img||''});
})JS";

static const wchar_t kExportViewerImgJs[] = LR"JS((function(){
var im=document.querySelector('img.MMImage-Origin,img.MMImage-Preview');
if(!im)return '';
try{
  var w=im.naturalWidth||im.width,h=im.naturalHeight||im.height;
  if(w<1||h<1)return pick(im)||'';
  var c=document.createElement('canvas');c.width=w;c.height=h;
  c.getContext('2d').drawImage(im,0,0);
  return c.toDataURL('image/png');
}catch(e){return pick(im)||'';}
})())JS";

bool isMediaUrl(const std::wstring& url) {
    std::wstring e = extensionFromUrl(url);
    return e == L".mp4" || e == L".webm" || e == L".mkv" || e == L".mp3" ||
           e == L".wav" || e == L".ogg" || e == L".m4a";
}

void copyText(HWND owner, const std::wstring& text) {
    if (!OpenClipboard(owner)) return;
    EmptyClipboard();
    HGLOBAL h = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (h) {
        memcpy(GlobalLock(h), text.c_str(), (text.size() + 1) * sizeof(wchar_t));
        GlobalUnlock(h);
        SetClipboardData(CF_UNICODETEXT, h);
    }
    CloseClipboard();
}

struct MenuEntry {
    std::wstring id;
    std::wstring label;
    bool sep = false;
    bool disabled = false;
};

std::wstring jsonEscape(const std::wstring& s) {
    std::wstring o;
    for (wchar_t c : s) {
        if (c == L'"' || c == L'\\') o += L'\\';
        if (c == L'\n') o += L"\\n";
        else if (c == L'\r') o += L"\\r";
        else if (c == L'\t') o += L"\\t";
        else o += c;
    }
    return o;
}

std::wstring menuToJson(const std::vector<MenuEntry>& items) {
    std::wstring j = L"[";
    for (size_t i = 0; i < items.size(); i++) {
        if (i) j += L',';
        if (items[i].sep) {
            j += L"{\"sep\":true}";
            continue;
        }
        j += L"{\"id\":\"" + jsonEscape(items[i].id) + L"\",\"label\":\"" +
             jsonEscape(items[i].label) + L"\"";
        if (items[i].disabled) j += L",\"disabled\":true";
        j += L"}";
    }
    j += L"]";
    return j;
}

void addSep(std::vector<MenuEntry>& m) {
    if (!m.empty() && !m.back().sep) m.push_back({L"", L"", true});
}

std::wstring contextMenuRuntimeScript() {
    std::wstring script = contextMenuInjectScript();
    return script.empty() ? kCtxMenuInline : script;
}

void showMenu(ICoreWebView2* sender, int x, int y, const std::vector<MenuEntry>& items) {
    if (!sender || items.empty()) return;
    std::wstring json = menuToJson(items);
    std::wstring script = L"(function(){try{";
    script += contextMenuRuntimeScript();
    script += L"}catch(e){}if(window.__ancoreHideCtxMenu)window.__ancoreHideCtxMenu();";
    script += L"if(window.__ancoreShowCtxMenu)window.__ancoreShowCtxMenu(";
    script += std::to_wstring(x) + L"," + std::to_wstring(y) + L"," + json + L");})()";
    sender->ExecuteScript(script.c_str(), nullptr);
}

void mergeProbe(const std::wstring& probeJson) {
    if (extractJsonString(probeJson, L"sel") == L"1")
        g_ctx.hasSelection = true;
    g_ctx.viewerOpen = extractJsonString(probeJson, L"viewer") == L"1";
    g_ctx.sidebarAction = extractJsonString(probeJson, L"sidebar") == L"1";

    // Любой сайт: в адресе вкладки прямой файл .png/.jpg/... → только меню изображения
    if (isDirectImagePageUrl(g_ctx.pageUri) && !g_ctx.hasSelection) {
        applyDirectImagePageContext();
        finalizeMenuContext();
        return;
    }

    g_ctx.hitText = extractJsonString(probeJson, L"textHit") == L"1";
    if (extractJsonString(probeJson, L"imgHit") == L"1")
        g_ctx.imageAtPoint = true;
    if (g_ctx.viewerOpen && g_ctx.sidebarAction)
        g_ctx.hitText = true;

    std::wstring img = extractJsonString(probeJson, L"img");
    std::wstring link = extractJsonString(probeJson, L"link");
    if (!img.empty() && g_ctx.imageAtPoint) {
        g_ctx.imageUri = img;
        g_ctx.saveKind = SaveKind::Image;
        g_ctx.saveUrl = img;
    }
    if (!link.empty() && g_ctx.linkUri.empty()) g_ctx.linkUri = link;
    if (g_ctx.imageUri.empty() && !link.empty() && !g_ctx.hitText) {
        std::wstring fromLink = imageUrlFromPageLink(link);
        if (!fromLink.empty()) {
            g_ctx.imageUri = fromLink;
            g_ctx.saveKind = SaveKind::Image;
            g_ctx.saveUrl = fromLink;
            g_ctx.imageAtPoint = true;
        }
    }
    if (g_ctx.saveKind == SaveKind::None && !link.empty() && !g_ctx.hitText) {
        if (isImageUrl(link)) {
            g_ctx.imageUri = link;
            g_ctx.saveKind = SaveKind::Image;
            g_ctx.saveUrl = link;
            g_ctx.imageAtPoint = true;
        } else if (isMediaUrl(link)) {
            g_ctx.mediaUri = link;
            g_ctx.saveKind = SaveKind::Media;
            g_ctx.saveUrl = link;
        } else if (!extensionFromUrl(link).empty()) {
            g_ctx.saveKind = SaveKind::File;
            g_ctx.saveUrl = link;
        }
    }
    applyLinkImageHints();
    applyYandexPageImageContext();
    applyDirectImagePageContext();
    finalizeMenuContext();
}

enum class MenuScene { Page, Image, Link, Media, Selection };

MenuScene detectMenuScene() {
    if (isDirectImagePageUrl(g_ctx.pageUri) && !g_ctx.hasSelection)
        return MenuScene::Image;
    if (g_ctx.hasSelection && !g_ctx.imageAtPoint)
        return MenuScene::Selection;
    if (g_ctx.sidebarAction)
        return MenuScene::Page;
    if (isYandexImageViewerPage(g_ctx.pageUri) && !g_ctx.imageUri.empty() && !g_ctx.hitText)
        return MenuScene::Image;
    if (g_ctx.viewerOpen && !g_ctx.imageUri.empty() && !g_ctx.sidebarAction)
        return MenuScene::Image;
    if (g_ctx.hitText && !g_ctx.imageAtPoint)
        return MenuScene::Page;
    if (g_ctx.imageAtPoint && (!g_ctx.imageUri.empty() || g_ctx.saveKind == SaveKind::Image))
        return MenuScene::Image;
    if (g_ctx.saveKind == SaveKind::Selection)
        return MenuScene::Selection;
    if (!g_ctx.mediaUri.empty() || g_ctx.saveKind == SaveKind::Media)
        return MenuScene::Media;
    if (!g_ctx.linkUri.empty())
        return MenuScene::Link;
    return MenuScene::Page;
}

std::vector<MenuEntry> buildMenu() {
    std::vector<MenuEntry> m;
    const auto scene = detectMenuScene();
    const auto& img = g_ctx.imageUri;
    const auto& link = g_ctx.linkUri;
    const auto& media = g_ctx.mediaUri;

    if (scene == MenuScene::Image) {
        m.push_back({L"saveImage", L"Сохранить изображение как..."});
        m.push_back({L"copyImage", L"Копировать изображение"});
        m.push_back({L"imageCopy", L"Копировать адрес изображения"});
        m.push_back({L"imageOpen", L"Открыть изображение в новой вкладке"});
        addSep(m);
        m.push_back({L"inspect", L"Просмотреть код элемента"});
        return m;
    }

    if (scene == MenuScene::Media) {
        m.push_back({L"saveMedia", g_ctx.isVideo ? L"Сохранить видео как..." : L"Сохранить аудио как..."});
        m.push_back({L"mediaCopy", L"Копировать адрес"});
        addSep(m);
        m.push_back({L"inspect", L"Просмотреть код элемента"});
        return m;
    }

    if (scene == MenuScene::Selection) {
        m.push_back({L"copySelection", L"Копировать"});
        m.push_back({L"saveSelection", L"Сохранить выделение как..."});
        addSep(m);
        m.push_back({L"inspect", L"Просмотреть код элемента"});
        return m;
    }

    if (scene == MenuScene::Link) {
        m.push_back({L"linkOpen", L"Открыть ссылку в новой вкладке"});
        m.push_back({L"linkCopy", L"Копировать адрес ссылки"});
        if (!imageUrlFromPageLink(link).empty())
            m.push_back({L"savePageImage", L"Сохранить изображение как..."});
        if (g_ctx.saveKind == SaveKind::File)
            m.push_back({L"saveFile", L"Сохранить файл как..."});
        else if (!isPhotoGalleryUrl(link) &&
                 (link.find(L"http://") == 0 || link.find(L"https://") == 0)) {
            if (!isWebPageUrl(link))
                m.push_back({L"downloadLink", L"Сохранить ссылку как..."});
            else
                m.push_back({L"downloadLink", L"Сохранить страницу как..."});
        }
        addSep(m);
        m.push_back({L"inspect", L"Просмотреть код элемента"});
        return m;
    }

    // Страница (как в Chrome: навигация, сохранить страницу, не путать с картинкой)
    m.push_back({L"back", L"Назад", false, !g_ctx.canBack});
    m.push_back({L"forward", L"Вперёд", false, !g_ctx.canForward});
    m.push_back({L"reload", L"Обновить"});
    addSep(m);
    m.push_back({L"savePage", L"Сохранить страницу как..."});
    m.push_back({L"print", L"Печать..."});
    m.push_back({L"viewSource", L"Просмотр кода страницы"});
    addSep(m);
    m.push_back({L"inspect", L"Просмотреть код элемента"});
    return m;
}

void parseTarget(ICoreWebView2ContextMenuTarget* target, COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND kind) {
    g_ctx.targetKind = kind;
    BOOL hasLink = FALSE;
    target->get_HasLinkUri(&hasLink);
    if (hasLink) {
        wchar_t* u = nullptr;
        if (SUCCEEDED(target->get_LinkUri(&u)) && u) {
            g_ctx.linkUri = u;
            CoTaskMemFree(u);
        }
    }
    if (kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_IMAGE) {
        g_ctx.imageAtPoint = true;
        BOOL hasSrc = FALSE;
        target->get_HasSourceUri(&hasSrc);
        if (hasSrc) {
            wchar_t* u = nullptr;
            if (SUCCEEDED(target->get_SourceUri(&u)) && u) {
                g_ctx.imageUri = u;
                g_ctx.saveUrl = u;
                g_ctx.saveKind = SaveKind::Image;
                CoTaskMemFree(u);
            }
        }
    }
    if (kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_VIDEO ||
        kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_AUDIO) {
        BOOL hasSrc = FALSE;
        target->get_HasSourceUri(&hasSrc);
        if (hasSrc) {
            wchar_t* u = nullptr;
            if (SUCCEEDED(target->get_SourceUri(&u)) && u) {
                g_ctx.mediaUri = u;
                g_ctx.saveUrl = u;
                g_ctx.saveKind = SaveKind::Media;
                g_ctx.isVideo = (kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_VIDEO);
                CoTaskMemFree(u);
            }
        }
    }
    if (kind == COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_SELECTED_TEXT) {
        g_ctx.hasSelection = true;
        g_ctx.saveKind = SaveKind::Selection;
    }

    if (g_ctx.saveKind == SaveKind::None && !g_ctx.linkUri.empty() && !g_ctx.hitText) {
        if (isImageUrl(g_ctx.linkUri)) {
            g_ctx.imageUri = g_ctx.linkUri;
            g_ctx.saveKind = SaveKind::Image;
            g_ctx.saveUrl = g_ctx.linkUri;
            g_ctx.imageAtPoint = true;
        } else if (isMediaUrl(g_ctx.linkUri)) {
            g_ctx.mediaUri = g_ctx.linkUri;
            g_ctx.saveKind = SaveKind::Media;
            g_ctx.saveUrl = g_ctx.linkUri;
        } else if (!extensionFromUrl(g_ctx.linkUri).empty()) {
            g_ctx.saveKind = SaveKind::File;
            g_ctx.saveUrl = g_ctx.linkUri;
        }
    }
}

} // namespace

static std::wstring loadTextFile(const std::wstring& path) {
    std::ifstream f(path, std::ios::binary);
    if (!f) return {};
    std::ostringstream ss;
    ss << f.rdbuf();
    std::string utf8 = ss.str();
    if (utf8.empty()) return {};
    if (utf8.size() >= 3 &&
        (unsigned char)utf8[0] == 0xEF &&
        (unsigned char)utf8[1] == 0xBB &&
        (unsigned char)utf8[2] == 0xBF)
        utf8 = utf8.substr(3);
    int n = MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), nullptr, 0);
    if (n <= 0) return {};
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, 0, utf8.c_str(), (int)utf8.size(), out.data(), n);
    return out;
}

std::wstring contextMenuInjectScript() {
    static std::wstring script;
    static bool tried = false;
    if (tried) return script;
    tried = true;

    std::wstring dir = BrowserCore::resourcesDirectory();
    if (!dir.empty())
        script = loadTextFile(dir + L"\\html\\ctxmenu-inject.js");
#ifdef ANCORE_RESOURCES_DIR
    if (script.empty())
        script = loadTextFile(std::wstring(L"" ANCORE_RESOURCES_DIR) + L"\\html\\ctxmenu-inject.js");
#endif
    if (script.empty())
        script = kCtxMenuInline;
    return script;
}


void handleContextMenuRequested(MainWindow* mw, ICoreWebView2* sender,
    ICoreWebView2ContextMenuRequestedEventArgs* args) {
    if (!mw || !sender || !args) return;

    Microsoft::WRL::ComPtr<ICoreWebView2Deferral> deferral;
    args->GetDeferral(&deferral);
    args->put_Handled(TRUE);

    g_ctx = {};
    COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND kind = COREWEBVIEW2_CONTEXT_MENU_TARGET_KIND_PAGE;
    ICoreWebView2ContextMenuTarget* target = nullptr;
    if (SUCCEEDED(args->get_ContextMenuTarget(&target)) && target) {
        target->get_Kind(&kind);
        parseTarget(target, kind);
        target->Release();
    }

    sender->get_CanGoBack(&g_ctx.canBack);
    sender->get_CanGoForward(&g_ctx.canForward);
    wchar_t* pageSrc = nullptr;
    if (SUCCEEDED(sender->get_Source(&pageSrc)) && pageSrc) {
        g_ctx.pageUri = pageSrc;
        CoTaskMemFree(pageSrc);
    }

    POINT pt = {};
    args->get_Location(&pt);
    const int px = pt.x, py = pt.y;
    g_ctx.menuX = px;
    g_ctx.menuY = py;
    primeDirectImagePageContext();
    applyLinkImageHints();

    std::wstring probeScript = std::wstring(kImgDetectHelpersJs) + kImageProbeBody;
    probeScript += L'(';
    probeScript += std::to_wstring(px);
    probeScript += L',';
    probeScript += std::to_wstring(py);
    probeScript += L')';

    sender->ExecuteScript(
        probeScript.c_str(),
        Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
            [mw, sender, deferral, px, py](HRESULT hr, LPCWSTR result) -> HRESULT {
                if (SUCCEEDED(hr) && result)
                    mergeProbe(unquoteScriptResult(result));
                showMenu(sender, px, py, buildMenu());
                if (deferral)
                    deferral->Complete();
                return S_OK;
            }).Get());
}

void handleContextMenuAction(MainWindow* mw, const std::wstring& action, ICoreWebView2* sender) {
    if (!mw || action.empty()) return;
    HWND owner = mw->handle();
    auto bc = BrowserCore::instance();

    if (action == L"linkOpen" && !g_ctx.linkUri.empty()) {
        bc->addTab(g_ctx.linkUri.c_str());
        return;
    }
    if (action == L"linkCopy" && !g_ctx.linkUri.empty()) {
        copyText(owner, g_ctx.linkUri);
        return;
    }
    if (action == L"imageOpen" && !g_ctx.imageUri.empty()) {
        bc->addTab(g_ctx.imageUri.c_str());
        return;
    }
    if (action == L"imageCopy" && !g_ctx.imageUri.empty()) {
        copyText(owner, g_ctx.imageUri);
        return;
    }
    if (action == L"mediaCopy" && !g_ctx.mediaUri.empty()) {
        copyText(owner, g_ctx.mediaUri);
        return;
    }
    if (action == L"copyImage" && !g_ctx.imageUri.empty() && sender) {
        std::wstring url = g_ctx.imageUri;
        for (size_t i = 0; i < url.size(); i++) {
            if (url[i] == L'\'') url.replace(i, 1, L"\\'");
        }
        sender->ExecuteScript(
            (L"(function(){var i=new Image();i.crossOrigin='anonymous';i.src='" +
             url + L"';i.onload=function(){"
             L"var c=document.createElement('canvas');c.width=i.width;c.height=i.height;"
             L"c.getContext('2d').drawImage(i,0,0);c.toBlob(function(b){"
             L"try{navigator.clipboard.write([new ClipboardItem({'image/png':b})]);}catch(e){}}"
             L",'image/png');};})();").c_str(),
            nullptr);
        return;
    }
    if (action == L"copySelection" && sender) {
        sender->ExecuteScript(
            L"(function(){var t=window.getSelection().toString();"
            L"if(t&&navigator.clipboard)navigator.clipboard.writeText(t);})()",
            nullptr);
        return;
    }
    if (action == L"saveImage" || action == L"saveFile" || action == L"saveMedia") {
        if (action == L"saveImage" && !g_ctx.imageAtPoint && g_ctx.imageUri.empty()) return;
        std::wstring saveUrl = g_ctx.saveUrl.empty() ? g_ctx.imageUri : g_ctx.saveUrl;
        if (saveUrl.empty()) return;
        saveUrl = resolveDownloadUrl(saveUrl);
        if (action == L"saveImage" &&
            (saveUrl.rfind(L"blob:", 0) == 0 || saveUrl.rfind(L"data:", 0) == 0) && sender) {
            std::wstring path;
            auto fmts = formatsForImageUrl(L"");
            if (fmts.empty() || !promptSaveFile(owner, fmts, L"image.png", path)) return;
            sender->ExecuteScript(
                kExportViewerImgJs,
                Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                    [owner, path](HRESULT hr, LPCWSTR result) -> HRESULT {
                        if (FAILED(hr) || !result) {
                            BrowserCore::instance()->setStatusText(L"Не удалось экспортировать изображение");
                            return S_OK;
                        }
                        std::wstring data = unquoteScriptResult(result);
                        if (data.empty() || data.rfind(L"data:image/", 0) != 0) {
                            BrowserCore::instance()->setStatusText(L"Не удалось экспортировать изображение");
                            return S_OK;
                        }
                        if (saveImageUrlAs(data, path)) registerManualSave(path);
                        return S_OK;
                    }).Get());
            return;
        }
        std::vector<SaveFormat> fmts;
        if (action == L"saveImage") fmts = formatsForImageUrl(saveUrl);
        else if (action == L"saveMedia")
            fmts = formatsForMediaUrl(saveUrl, g_ctx.isVideo);
        else
            fmts = formatsForFileUrl(saveUrl);
        if (fmts.empty()) return;
        std::wstring path;
        std::wstring suggest = (action == L"saveImage")
            ? suggestedImageFileName(saveUrl) : fileNameFromUrl(saveUrl);
        if (!promptSaveFile(owner, fmts, suggest, path)) return;
        if (action == L"saveImage" && sender) {
            saveImageUrlAsWithWebView(sender, saveUrl, path, [path](bool ok) {
                if (ok) registerManualSave(path);
                else
                    BrowserCore::instance()->setStatusText(
                        L"Не удалось сохранить изображение (сеть или ограничения сайта)");
            });
            return;
        }
        bool ok = (action == L"saveImage") ? saveImageUrlAs(saveUrl, path)
                                            : downloadUrlToPath(saveUrl, path);
        if (ok) registerManualSave(path);
        return;
    }
    if (action == L"downloadLink" && !g_ctx.linkUri.empty()) {
        std::wstring resolved = resolveDownloadUrl(g_ctx.linkUri);
        if (isImageUrl(resolved)) {
            std::wstring path;
            auto fmts = formatsForImageUrl(resolved);
            if (fmts.empty() || !promptSaveFile(owner, fmts, suggestedImageFileName(resolved), path))
                return;
            if (sender) {
                saveImageUrlAsWithWebView(sender, resolved, path, [path](bool ok) {
                    if (ok) registerManualSave(path);
                    else
                        BrowserCore::instance()->setStatusText(
                            L"Не удалось сохранить изображение (сеть или ограничения сайта)");
                });
            } else if (saveImageUrlAs(resolved, path)) {
                registerManualSave(path);
            }
            return;
        }
        std::wstring path;
        auto fmts = formatsForFileUrl(g_ctx.linkUri);
        if (fmts.empty() || (fmts.size() == 1 && fmts[0].ext.empty()))
            fmts = {{L"Web page (*.html;*.htm)", L"*.html;*.htm", L".html"}};
        if (!promptSaveFile(owner, fmts, suggestedDownloadFileName(g_ctx.linkUri), path)) return;
        if (downloadUrlToPath(g_ctx.linkUri, path)) registerManualSave(path);
        return;
    }
    if (action == L"savePageImage" && sender) {
        if (!g_ctx.imageAtPoint && g_ctx.imageUri.empty() &&
            !isYandexImageViewerPage(g_ctx.pageUri)) {
            BrowserCore::instance()->setStatusText(
                L"Под курсором нет изображения — выберите пункт на самой картинке");
            return;
        }
        std::wstring script = std::wstring(kImgDetectHelpersJs) + kSavePageImageBody;
        script += L'(';
        script += std::to_wstring(g_ctx.menuX);
        script += L',';
        script += std::to_wstring(g_ctx.menuY);
        script += L')';
        sender->ExecuteScript(
            script.c_str(),
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [sender, owner](HRESULT hr, LPCWSTR result) -> HRESULT {
                    if (FAILED(hr) || !result) return S_OK;
                    std::wstring img = extractJsonString(unquoteScriptResult(result), L"img");
                    if (img.empty())
                        img = imageUrlFromPageLink(g_ctx.linkUri);
                    if (img.empty())
                        img = imageUrlFromPageLink(g_ctx.pageUri);
                    if (img.empty()) {
                        BrowserCore::instance()->setStatusText(
                            L"Не удалось найти изображение на странице");
                        return S_OK;
                    }
                    img = resolveDownloadUrl(img);
                    std::wstring path;
                    auto fmts = formatsForImageUrl(img);
                    if (fmts.empty() || !promptSaveFile(owner, fmts, suggestedImageFileName(img), path))
                        return S_OK;
                    if (img.rfind(L"blob:", 0) == 0) {
                        sender->ExecuteScript(
                            kExportViewerImgJs,
                            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                                [path](HRESULT hr2, LPCWSTR result2) -> HRESULT {
                                    if (FAILED(hr2) || !result2) return S_OK;
                                    std::wstring data = unquoteScriptResult(result2);
                                    if (data.rfind(L"data:image/", 0) == 0 &&
                                        saveImageUrlAs(data, path))
                                        registerManualSave(path);
                                    else
                                        BrowserCore::instance()->setStatusText(
                                            L"Не удалось экспортировать изображение");
                                    return S_OK;
                                }).Get());
                        return S_OK;
                    }
                    saveImageUrlAsWithWebView(sender, img, path, [path](bool ok) {
                        if (ok) registerManualSave(path);
                        else
                            BrowserCore::instance()->setStatusText(
                                L"Не удалось сохранить изображение (сеть или ограничения сайта)");
                    });
                    return S_OK;
                }).Get());
        return;
    }
    if (action == L"saveSelection" && sender) {
        std::wstring path;
        if (!promptSaveFile(owner, formatsForSelectedText(), L"selection.txt", path)) return;
        sender->ExecuteScript(
            L"(function(){var t=window.getSelection().toString();return t?t:'';})()",
            Callback<ICoreWebView2ExecuteScriptCompletedHandler>(
                [path](HRESULT hr, LPCWSTR result) -> HRESULT {
                    if (FAILED(hr) || !result) return S_OK;
                    std::wstring text = unquoteScriptResult(result);
                    FILE* f = nullptr;
                    if (_wfopen_s(&f, path.c_str(), L"wb") != 0 || !f) return S_OK;
                    unsigned char bom[] = {0xEF, 0xBB, 0xBF};
                    fwrite(bom, 1, 3, f);
                    int len = WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                        nullptr, 0, nullptr, nullptr);
                    if (len > 0) {
                        std::string utf8(len, 0);
                        WideCharToMultiByte(CP_UTF8, 0, text.c_str(), (int)text.size(),
                            utf8.data(), len, nullptr, nullptr);
                        fwrite(utf8.data(), 1, len, f);
                    }
                    fclose(f);
                    registerManualSave(path);
                    return S_OK;
                }).Get());
        return;
    }
    if (action == L"back") { if (sender) sender->GoBack(); return; }
    if (action == L"forward") { if (sender) sender->GoForward(); return; }
    if (action == L"reload") { if (sender) sender->Reload(); return; }
    if (action == L"savePage") {
        if (isPhotoGalleryUrl(g_ctx.pageUri) || isYandexImageViewerPage(g_ctx.pageUri) ||
            isDirectImagePageUrl(g_ctx.pageUri) || g_ctx.viewerOpen) {
            BrowserCore::instance()->setStatusText(
                L"Здесь сохраняется изображение: ПКМ по картинке → «Сохранить изображение как...»");
            return;
        }
        savePage(sender, owner);
        return;
    }
    if (action == L"print" && sender) {
        ICoreWebView2_16* v16 = nullptr;
        if (SUCCEEDED(sender->QueryInterface(IID_ICoreWebView2_16, (void**)&v16))) {
            v16->ShowPrintUI(COREWEBVIEW2_PRINT_DIALOG_KIND_BROWSER);
            v16->Release();
        }
        return;
    }
    if (action == L"viewSource" && sender) {
        wchar_t* curUrl = nullptr;
        if (SUCCEEDED(sender->get_Source(&curUrl)) && curUrl) {
            sender->Navigate((std::wstring(L"view-source:") + curUrl).c_str());
            CoTaskMemFree(curUrl);
        }
        return;
    }
    if (action == L"inspect" && sender) sender->OpenDevToolsWindow();
}
