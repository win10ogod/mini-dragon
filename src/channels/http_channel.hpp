#pragma once
#include "channel.hpp"
#include "../config.hpp"
#include "../rate_limiter.hpp"
#include <httplib.h>
#include <nlohmann/json.hpp>
#include <thread>
#include <iostream>

namespace minidragon {

static const char* WEB_CHAT_HTML = R"HTML(<!DOCTYPE html>
<html lang="en">
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1.0">
<title>Mini Dragon</title>
<style>
*{margin:0;padding:0;box-sizing:border-box}
:root{--bg:#0a0e17;--surface:#111827;--surface2:#1a2235;--border:#1e2d4a;--cyan:#00e5ff;--green:#39ff14;--text:#e0e6ed;--dim:#6b7a90;--user-bg:#0d2137;--assist-bg:#111827;--err:#ff4757}
body{font-family:'Segoe UI',system-ui,-apple-system,sans-serif;background:var(--bg);color:var(--text);height:100vh;display:flex;flex-direction:column;overflow:hidden}
#header{background:var(--surface);border-bottom:1px solid var(--border);padding:12px 20px;display:flex;align-items:center;gap:12px;flex-shrink:0}
#header h1{font-size:16px;font-weight:600;color:var(--cyan);letter-spacing:0.5px}
#header h1 span{color:var(--green)}
#status{width:8px;height:8px;border-radius:50%;background:var(--green);flex-shrink:0}
#status.err{background:var(--err)}
#status.wait{background:#ffb300;animation:pulse 1s infinite}
@keyframes pulse{0%,100%{opacity:1}50%{opacity:0.4}}
#msgs{flex:1;overflow-y:auto;padding:16px;display:flex;flex-direction:column;gap:2px}
.msg{padding:10px 16px;border-radius:8px;max-width:85%;line-height:1.6;font-size:14px;white-space:pre-wrap;word-break:break-word}
.msg.user{background:var(--user-bg);border:1px solid #163a5c;align-self:flex-end;color:#b8d4e8}
.msg.assistant{background:var(--assist-bg);border:1px solid var(--border);align-self:flex-start}
.msg.system{background:transparent;border:1px solid var(--border);align-self:center;color:var(--dim);font-size:13px;font-style:italic}
.msg.assistant .role{color:var(--cyan);font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;margin-bottom:4px}
.msg.user .role{color:#4a9ede;font-size:11px;font-weight:600;text-transform:uppercase;letter-spacing:1px;margin-bottom:4px}
.msg.assistant code{background:#1a2744;padding:1px 5px;border-radius:3px;font-family:'Cascadia Code','Fira Code',monospace;font-size:13px;color:var(--green)}
.msg.assistant pre{background:#0d1520;border:1px solid var(--border);border-radius:6px;padding:12px;margin:8px 0;overflow-x:auto}
.msg.assistant pre code{background:none;padding:0;color:#c8d6e5}
.msg.assistant strong{color:var(--cyan)}
.msg.assistant ul,.msg.assistant ol{margin:4px 0 4px 20px}
.msg.assistant a{color:var(--cyan);text-decoration:underline}
#thinking{display:none;padding:8px 16px;color:var(--dim);font-size:13px;align-self:flex-start}
#thinking.show{display:flex;align-items:center;gap:8px}
#thinking .dots span{animation:dot 1.4s infinite;opacity:0;font-size:18px}
#thinking .dots span:nth-child(2){animation-delay:0.2s}
#thinking .dots span:nth-child(3){animation-delay:0.4s}
@keyframes dot{0%,60%,100%{opacity:0}30%{opacity:1}}
#input-area{background:var(--surface);border-top:1px solid var(--border);padding:12px 16px;flex-shrink:0;display:flex;gap:10px;align-items:flex-end}
#input{flex:1;background:var(--surface2);border:1px solid var(--border);border-radius:8px;color:var(--text);padding:10px 14px;font-size:14px;font-family:inherit;resize:none;min-height:42px;max-height:150px;line-height:1.5;outline:none;transition:border-color 0.2s}
#input:focus{border-color:var(--cyan)}
#send{background:var(--cyan);color:#000;border:none;border-radius:8px;padding:10px 20px;font-size:14px;font-weight:600;cursor:pointer;transition:opacity 0.2s;white-space:nowrap}
#send:hover{opacity:0.85}
#send:disabled{opacity:0.4;cursor:not-allowed}
.msg.assistant .content p{margin:4px 0}
@media(max-width:600px){
  .msg{max-width:95%}
  #input-area{padding:8px 10px}
  #send{padding:10px 14px}
  #header{padding:10px 14px}
}
</style>
</head>
<body>
<div id="header">
  <div id="status"></div>
  <h1>Mini <span>Dragon</span></h1>
</div>
<div id="msgs">
  <div class="msg system">Connected. Type a message to begin.</div>
</div>
<div id="thinking"><span>Thinking</span><span class="dots"><span>.</span><span>.</span><span>.</span></span></div>
<div id="input-area">
  <textarea id="input" rows="1" placeholder="Send a message..." autofocus></textarea>
  <button id="send">Send</button>
</div>
<script>
const msgs=document.getElementById('msgs');
const input=document.getElementById('input');
const sendBtn=document.getElementById('send');
const status=document.getElementById('status');
const thinking=document.getElementById('thinking');
let busy=false;

function autoGrow(){
  input.style.height='auto';
  input.style.height=Math.min(input.scrollHeight,150)+'px';
}
input.addEventListener('input',autoGrow);

function scrollBottom(){
  msgs.scrollTop=msgs.scrollHeight;
}

function renderMd(text){
  let h=text
    .replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;')
    .replace(/```(\w*)\n([\s\S]*?)```/g,(_,lang,code)=>'<pre><code>'+code.trim()+'</code></pre>')
    .replace(/`([^`]+)`/g,'<code>$1</code>')
    .replace(/\*\*([^*]+)\*\*/g,'<strong>$1</strong>')
    .replace(/\*([^*]+)\*/g,'<em>$1</em>')
    .replace(/^\s*[-*]\s+(.+)/gm,'<li>$1</li>')
    .replace(/(<li>.*<\/li>)/gs,'<ul>$1</ul>')
    .replace(/<\/ul>\s*<ul>/g,'')
    .replace(/\[([^\]]+)\]\((https?:\/\/[^)]+)\)/g,'<a href="$2" target="_blank" rel="noopener">$1</a>')
    .replace(/\n{2,}/g,'</p><p>')
    .replace(/\n/g,'<br>');
  return '<p>'+h+'</p>';
}

function addMsg(role,text){
  const d=document.createElement('div');
  d.className='msg '+role;
  if(role==='assistant'){
    d.innerHTML='<div class="role">assistant</div><div class="content">'+renderMd(text)+'</div>';
  } else if(role==='user'){
    d.innerHTML='<div class="role">you</div>'+text.replace(/&/g,'&amp;').replace(/</g,'&lt;').replace(/>/g,'&gt;');
  } else {
    d.textContent=text;
  }
  msgs.appendChild(d);
  scrollBottom();
  return d;
}

function createAssistantMsg(){
  const d=document.createElement('div');
  d.className='msg assistant';
  d.innerHTML='<div class="role">assistant</div><div class="content"></div>';
  msgs.appendChild(d);
  scrollBottom();
  return d;
}

async function send(){
  const text=input.value.trim();
  if(!text||busy)return;
  busy=true;
  sendBtn.disabled=true;
  input.value='';
  autoGrow();
  addMsg('user',text);
  status.className='wait';
  thinking.className='show';

  let fullText='';
  const el=createAssistantMsg();
  const content=el.querySelector('.content');

  try{
    const res=await fetch('/chat/stream',{
      method:'POST',
      headers:{'Content-Type':'application/json'},
      body:JSON.stringify({text:text,user:'web',channel:'http'})
    });
    if(!res.ok)throw new Error('stream_fail');
    const reader=res.body.getReader();
    const dec=new TextDecoder();
    let buf='';
    while(true){
      const{done,value}=await reader.read();
      if(done)break;
      buf+=dec.decode(value,{stream:true});
      const lines=buf.split('\n');
      buf=lines.pop()||'';
      for(const line of lines){
        if(!line.startsWith('data: '))continue;
        const payload=line.slice(6).trim();
        if(payload==='[DONE]')continue;
        try{
          const j=JSON.parse(payload);
          const delta=j.choices&&j.choices[0]&&j.choices[0].delta;
          if(delta&&delta.content){
            fullText+=delta.content;
            content.innerHTML=renderMd(fullText);
            scrollBottom();
          }
        }catch(e){}
      }
    }
    if(!fullText)throw new Error('no_content');
    status.className='';
  }catch(e){
    try{
      const fallback=await fetch('/chat',{
        method:'POST',
        headers:{'Content-Type':'application/json'},
        body:JSON.stringify({text:text,user:'web',channel:'http'})
      });
      const fj=await fallback.json();
      fullText=fj.reply||fj.error||'No response';
      content.innerHTML=renderMd(fullText);
      scrollBottom();
      status.className='';
    }catch(e2){
      content.innerHTML='<span style="color:var(--err)">Error: '+e2.message+'</span>';
      status.className='err';
      setTimeout(()=>{status.className='';},3000);
    }
  }
  thinking.className='';
  busy=false;
  sendBtn.disabled=false;
  input.focus();
}

sendBtn.addEventListener('click',send);
input.addEventListener('keydown',e=>{
  if(e.key==='Enter'&&!e.shiftKey){e.preventDefault();send();}
});

fetch('/health').then(r=>r.json()).then(()=>{status.className='';}).catch(()=>{status.className='err';});
</script>
</body>
</html>)HTML";

class HTTPChannel : public Channel {
public:
    HTTPChannel(const std::string& host, int port, const HTTPChannelConfig& cfg)
        : host_(host), port_(port), config_(cfg), rate_limiter_(cfg.rate_limit_rpm) {}

    // Backward-compatible constructor
    HTTPChannel(const std::string& host, int port, bool enabled_flag)
        : host_(host), port_(port), rate_limiter_(0) {
        config_.enabled = enabled_flag;
    }

    std::string name() const override { return "http"; }
    bool enabled() const override { return config_.enabled; }

    void start(MessageHandler handler) override {
        if (!config_.enabled) return;
        handler_ = std::move(handler);

        server_.Get("/", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(WEB_CHAT_HTML, "text/html");
        });

        server_.Get("/health", [](const httplib::Request&, httplib::Response& res) {
            res.set_content(R"({"status":"ok"})", "application/json");
        });

        // Global exception handler for httplib
        server_.set_exception_handler([](const httplib::Request&, httplib::Response& res, std::exception_ptr ep) {
            std::string msg = "unknown error";
            try { if (ep) std::rethrow_exception(ep); }
            catch (const std::exception& e) { msg = e.what(); }
            catch (...) { msg = "non-std exception"; }
            std::cerr << "[http] Unhandled exception: " << msg << "\n";
            res.status = 500;
            nlohmann::json err;
            err["error"] = msg;
            res.set_content(err.dump(), "application/json");
        });

        server_.Post("/chat", [this](const httplib::Request& req, httplib::Response& res) {
            if (!check_auth(req, res)) return;
            if (!check_rate_limit(res)) return;

            nlohmann::json j;
            try {
                j = nlohmann::json::parse(req.body);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"invalid JSON in request body"})", "application/json");
                return;
            }

            InboundMessage msg;
            msg.channel = j.value("channel", "http");
            msg.user = j.value("user", "anonymous");
            msg.text = j.value("text", "");

            std::string reply;
            try {
                reply = handler_(msg);
            } catch (const std::exception& e) {
                std::cerr << "[http] /chat handler error: " << e.what() << "\n";
                reply = std::string("[error] ") + e.what();
            } catch (...) {
                std::cerr << "[http] /chat handler unknown error\n";
                reply = "[error] Unknown internal error";
            }

            nlohmann::json resp;
            resp["reply"] = reply;
            res.set_content(resp.dump(), "application/json");
        });

        server_.Post("/chat/stream", [this](const httplib::Request& req, httplib::Response& res) {
            if (!check_auth(req, res)) return;
            if (!check_rate_limit(res)) return;

            nlohmann::json j;
            try {
                j = nlohmann::json::parse(req.body);
            } catch (...) {
                res.status = 400;
                res.set_content(R"({"error":"invalid JSON in request body"})", "application/json");
                return;
            }

            InboundMessage msg;
            msg.channel = j.value("channel", "http");
            msg.user = j.value("user", "anonymous");
            msg.text = j.value("text", "");

            std::string reply;
            try {
                reply = handler_(msg);
            } catch (const std::exception& e) {
                std::cerr << "[http] /chat/stream handler error: " << e.what() << "\n";
                reply = std::string("[error] ") + e.what();
            } catch (...) {
                std::cerr << "[http] /chat/stream handler unknown error\n";
                reply = "[error] Unknown internal error";
            }

            res.set_header("Content-Type", "text/event-stream");
            res.set_header("Cache-Control", "no-cache");
            res.set_header("Connection", "keep-alive");

            // Stream the reply as SSE events in chunks
            std::string body;
            size_t chunk_size = 20;
            for (size_t i = 0; i < reply.size(); i += chunk_size) {
                std::string chunk = reply.substr(i, std::min(chunk_size, reply.size() - i));
                nlohmann::json event;
                event["choices"] = nlohmann::json::array();
                nlohmann::json choice;
                choice["delta"]["content"] = chunk;
                event["choices"].push_back(choice);
                body += "data: " + event.dump() + "\n\n";
            }
            body += "data: [DONE]\n\n";
            res.set_content(body, "text/event-stream");
        });

        thread_ = std::thread([this]() {
            std::cerr << "[http] Listening on " << host_ << ":" << port_ << "\n";
            server_.listen(host_, port_);
        });
    }

    void stop() override {
        server_.stop();
        if (thread_.joinable()) thread_.join();
    }

private:
    std::string host_;
    int port_;
    HTTPChannelConfig config_;
    MessageHandler handler_;
    httplib::Server server_;
    std::thread thread_;
    RateLimiter rate_limiter_;

    bool check_auth(const httplib::Request& req, httplib::Response& res) {
        if (config_.api_key.empty()) return true;

        auto auth = req.get_header_value("Authorization");
        if (auth != "Bearer " + config_.api_key) {
            res.status = 401;
            res.set_content(R"({"error":"unauthorized"})", "application/json");
            return false;
        }
        return true;
    }

    bool check_rate_limit(httplib::Response& res) {
        if (!rate_limiter_.allow()) {
            res.status = 429;
            res.set_content(R"({"error":"rate limit exceeded"})", "application/json");
            return false;
        }
        return true;
    }
};

} // namespace minidragon
