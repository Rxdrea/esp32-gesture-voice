import pathlib,shutil,subprocess,tempfile,os,csv,json,hashlib
ROOT=pathlib.Path(__file__).resolve().parent
SRC=ROOT.parent/'GestureVoice'
DATA=pathlib.Path(os.environ['GESTURE_TEST_DATA']) if os.environ.get('GESTURE_TEST_DATA') else None
CXX=pathlib.Path(os.environ.get('CXX') or shutil.which('g++') or 'g++')
def main():
 s=pathlib.Path(tempfile.mkdtemp(prefix='pinyin-host-',dir='C:/temp' if pathlib.Path('C:/temp').exists() else None))
 hashes={}
 for p in [SRC/'GestureVoice.ino',*[p for p in SRC.glob('*.h') if p.name!='PrivateConfig.h']]:
  shutil.copy2(p,s/p.name);hashes[p.name]=hashlib.sha256(p.read_bytes()).hexdigest()
 shutil.copytree(SRC/'tests/host_stubs',s/'stubs')
 for p in (ROOT/'stubs').glob('*') if (ROOT/'stubs').exists() else []: shutil.copy2(p,s/'stubs'/p.name)
 a=s/'stubs/Arduino.h';t=a.read_text();t+='\nusing std::max;\n';t=t.replace('#define INPUT 0','#define INPUT 0\n#define INPUT_PULLUP 2');t=t.replace('int indexOf(char c) const', 'int indexOf(const char* s, size_t start=0) const { auto p=value_.find(s,start); return p==std::string::npos?-1:int(p); }\n  int indexOf(char c,size_t start) const { auto p=value_.find(c,start); return p==std::string::npos?-1:int(p); }\n  int indexOf(char c) const');t=t.replace('size_t write(uint8_t c)', 'int availableForWrite() const {return 4096;}\n  size_t write(const uint8_t* p,size_t n) {output.append((const char*)p,n);return n;}\n  size_t write(uint8_t c)');a.write_text(t)
 u=s/'stubs/U8g2lib.h';t=u.read_text().replace('void sendBuffer() {}','void sendBuffer() {}\n  void updateDisplayArea(int,int,int,int) {}');u.write_text(t)
 e=s/'stubs/CheezsEMG.h';t=e.read_text().replace('return 1.0f;', 'return float(raw_);').replace('return 2;', 'return raw_;');e.write_text(t)
 w=s/'stubs/Wire.h';t=w.read_text().replace('void begin(int a,int b) { sda=a; scl=b; }','void begin(int a,int b) { if(registers.empty()) reset(); sda=a; scl=b; }');w.write_text(t)
 task=s/'stubs/freertos/task.h';t=task.read_text().replace('static bool stopSampler=false;', 'static unsigned taskStack=0,taskPriority=0; static int taskCore=-1; static std::string taskName; static bool stopSampler=false;').replace('const char*,unsigned,void*,unsigned,void*,int)', 'const char* name,unsigned stack,void*,unsigned priority,void*,int core)').replace('host::sampleTaskFn=fn;', 'host::taskName=name;host::taskStack=stack;host::taskPriority=priority;host::taskCore=core;host::sampleTaskFn=fn;');task.write_text(t)
 (s/'PrivateConfig.h').write_text('#pragma once\nconst char* WIFI_SSID="offline";\nconst char* WIFI_PASS="fake";\nconst char* API_KEY="fake";\n')
 shutil.copy2(ROOT/'business.cpp',s/'business.cpp')
 env=os.environ.copy();env['PATH']=str(CXX.parent)+os.pathsep+env['PATH']
 cmd=[str(CXX),'-std=gnu++17','-I'+str(s/'stubs'),'-I'+os.environ.get('ARDUINOJSON_INCLUDE',str(pathlib.Path.home()/'Documents/Arduino/libraries/ArduinoJson/src')),str(s/'business.cpp'),'-o',str(s/'verify.exe')]
 r=subprocess.run(cmd,capture_output=True,text=True,encoding="utf-8",errors="replace",env=env);(ROOT/'compile.log').write_text(r.stdout+r.stderr,encoding='utf-8');print('SCRATCH',s);print(r.stderr[-6000:])
 (ROOT/'source_hashes.json').write_text(json.dumps(hashes,indent=2))
 if r.returncode:return r.returncode
 results=[]
 cases=['setup','sample_led','sample_unworn','sample_warmup','reset_core','reset_wrist','mapping','utf8','unauthorized','busy','no_wear_x','candidate_http','speech']
 cases+=['scroll_up','scroll_down','scroll_candidates','scroll_jitter','scroll_side','scroll_partial_release','scroll_sensor_gap','scroll_direction_change']
 cases+=['http_connect','http_timeout','http_400','http_401','http_403','http_429','http_500','http_arrearage','http_invalid_key','http_free_quota','http_unsafe_code','http_bad_json','http_empty','http_bad_content','http_object','http_error_200','http_codeblock']
 for case in cases:
  args=[str(s/'verify.exe'),case]
  if case in ['reset_core','reset_wrist'] and DATA is None:
   print('SKIP',case,'requires GESTURE_TEST_DATA');continue
  if case in ['reset_core','reset_wrist']:
   label='grip' if case=='reset_core' else 'up'
   rows=list(csv.DictReader((DATA/label/f'{label}_008.csv').open(encoding='utf-8-sig')))
   trace=s/(case+'.txt');trace.write_text(str(len(rows))+'\n'+'\n'.join(' '.join(row[k] for k in ['ax','ay','az','gx','gy','gz','emg','worn']) for row in rows));args.append(str(trace))
  p=subprocess.run(args,capture_output=True,text=True,encoding="utf-8",errors="replace",env=env);results.append(dict(case=case,passed=p.returncode==0,output=p.stdout+p.stderr));print(results[-1])
 manifest=[]
 for label in (['up','down','clockwise','counterclockwise','right_side','grip','rest'] if DATA is not None else []):
  for n in [8,9,10]:
   path=DATA/label/f'{label}_{n:03}.csv'
   rows=list(csv.DictReader(path.open(encoding='utf-8-sig')))
   trace=s/'trace.txt';trace.write_text(str(len(rows))+'\n'+'\n'.join(' '.join(row[k] for k in ['ax','ay','az','gx','gy','gz','emg','worn']) for row in rows))
   p=subprocess.run([str(s/'verify.exe'),'replay',label,str(trace)],capture_output=True,text=True,encoding="utf-8",errors="replace",env=env)
   results.append(dict(case=f'{label}_{n:03}',passed=p.returncode==0,output=p.stdout+p.stderr));print(results[-1])
   for mode in ['unworn']+({'up':['cand_up'],'down':['cand_down'],'right_side':['cand_confirm']}.get(label,[])):
    q=subprocess.run([str(s/'verify.exe'),mode,label,str(trace)],capture_output=True,text=True,encoding="utf-8",errors="replace",env=env)
    results.append(dict(case=f'{mode}_{label}_{n:03}',passed=q.returncode==0,output=q.stdout+q.stderr));print(results[-1])
   manifest.append(dict(path=str(path),split='validation',sha256=hashlib.sha256(path.read_bytes()).hexdigest()))
 (ROOT/'manifest.json').write_text(json.dumps(dict(train='001-007',validation='008-010',files=manifest),ensure_ascii=False,indent=2),encoding='utf-8')
 (ROOT/'results.json').write_text(json.dumps(results,ensure_ascii=False,indent=2),encoding='utf-8')
 print('PASS',sum(x['passed'] for x in results),'/',len(results))
 return int(any(not x['passed'] for x in results))
if __name__=='__main__':raise SystemExit(main())
