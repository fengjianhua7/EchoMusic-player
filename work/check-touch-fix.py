from pathlib import Path
import shlex,subprocess
root=Path.cwd()
line=[x for x in (root/'work/esp32-storage-compile.log').read_text(encoding='utf-8',errors='ignore').splitlines() if 'xtensa-esp32s3-elf-g++ -c ' in x][-1]
args=[];skip=False
for x in shlex.split(line,posix=False):
 if skip:skip=False;continue
 if x in ['-o','-MF']:skip=True;continue
 if x in ['-E','-CC','-MMD'] or x.endswith(('.merged','.cpp','.c')):continue
 args.append(x.replace('outputs\\ESP32_Storage_Player','outputs\\ESP32_Touch_Fix'))
src=root/'work/touch-fix-syntax.cpp';src.write_text('#include <Arduino.h>\n'+(root/'outputs/ESP32_Touch_Fix/ESP32_Touch_Fix.ino').read_text(encoding='utf-8'),encoding='utf-8')
args+=['-fsyntax-only','-I'+str(root/'outputs/ESP32_Touch_Fix')]
args+=['-I'+str(p) for p in Path('C:/Users/fengjianhua/AppData/Local/Arduino15/packages/esp32/hardware/esp32/3.3.12/libraries').glob('*/src')]
args+=[str(src)]
r=subprocess.run(args,capture_output=True,text=True,timeout=90)
print(r.stdout,r.stderr);print('exit',r.returncode)
raise SystemExit(r.returncode)
