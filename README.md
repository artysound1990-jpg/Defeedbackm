# DeFeedback

กันเสียงหอน (feedback) + จัดการเรโซแนนซ์ของไมค์ สำหรับงานไลฟ์

มีสองแบบในตัวติดตั้งเดียว เลือกได้ตอนติดตั้ง
- **โปรแกรมเดี่ยว** เปิดใช้ได้เลย ไม่ต้องมีโปรแกรมทำเพลง เลือกไมค์กับลำโพงในตัวโปรแกรม
- **ปลั๊กอิน VST3** สำหรับ Cubase, Reaper, SuperRack Performer ฯลฯ

## สายสัญญาณ

```
Input -> Low Cut -> Resonance bells (dynamic) -> Feedback notches -> Output
```

สองตัวตรวจจับแยกกัน ทำงานคนละหน้าที่:

| | Feedback | Resonance |
|---|---|---|
| จับอะไร | tone แคบมากที่ค้างนาน | ย่านกว้าง ~1/4-1/2 octave ที่โด่งตลอด |
| มาจาก | ลูปไมค์-ลำโพง | สีของไมค์เอง / proximity / ห้อง |
| ใช้ฟิลเตอร์ | notch ลึก Q สูง | bell กว้าง Q ต่ำ |
| ลดเมื่อไหร่ | ลดถาวรทันทีที่เจอ | ลดเฉพาะตอนย่านนั้นเด้งขึ้นมา (dynamic EQ) |
| ตัดสินจาก | สเปกตรัมทันที | สเปกตรัมเฉลี่ย 3 วินาที (LTAS) |

## วิธีแยกแยะ (ผ่านการทดสอบแล้ว)

ตัว resonance เทียบ smooth แคบ (1/12 octave) กับ smooth กว้าง (3/4 octave) บนแกน log ความถี่
- tone แคบ ๆ จะถูกเฉลี่ยจนเหลือ +2.3 dB -> ต่ำกว่า threshold 4 dB จึงไม่โดน bell
- resonance 6 dB กว้าง 0.3 octave เหลือ +4.4 dB -> เกิน threshold จึงโดน bell
- ความชันของสเปกตรัมล้วน ๆ (pink) ให้ +0.8 dB -> ไม่โดนตัดมั่ว

เสริมอีกชั้น: ตัววิเคราะห์อ่านสัญญาณ**หลัง**ฟิลเตอร์ พอ notch ลงไปแล้ว tone จะหายจากสเปกตรัม
และ bin ที่อยู่ใกล้ notch ที่ทำงานอยู่จะถูกข้ามไปเลย

## พารามิเตอร์

**Feedback**
- `FB Peak Threshold` (15 dB) — จับไวขึ้นถ้าลดค่า
- `FB Detect Time` (110 ms) — ต้องค้างนานแค่ไหนถึงนับว่าหอน เพิ่มค่าถ้าไปกินโน้ตที่ลากยาว
- `FB Notch Q` (35) — ยิ่งสูงยิ่งแคบ กินเสียงน้อย
- `FB Max Notches` (8, สูงสุด 16)

**Resonance**
- `Resonance On` (เปิด)
- `Res Threshold` (4 dB) — ลดเป็น 3 ถ้าอยากจับ resonance เบา ๆ ด้วย
- `Res Depth` (60%) — ลดลึกแค่ไหนเทียบกับส่วนที่เกิน threshold
- `Res Q` (6) — ความกว้างของ bell
- `Res Max Bands` (4, สูงสุด 8)

**ทั่วไป**
- `Low Cut` (80 Hz) — ตัด rumble/pop ก่อนเข้าตัววิเคราะห์
- `Min Level` (-45 dBFS) — ต่ำกว่านี้ไม่วิเคราะห์ กันไม่ให้ตัดตอนเงียบ
- `Reset All` — ล้าง notch, bell และ LTAS ทั้งหมด

## บิ้วตัวติดตั้ง

### ทาง A: ไม่ต้องลงอะไรในเครื่อง (แนะนำ)

push โฟลเดอร์นี้ขึ้น GitHub เท่านั้น — CI จะบิ้วปลั๊กอินแล้วห่อเป็นตัวติดตั้งให้เอง
เสร็จแล้วเข้าแท็บ **Actions** > เลือก run ล่าสุด > โหลด artifact ชื่อ `DeFeedback-Setup`
ในนั้นคือ `DeFeedback-Setup-0.2.0.exe` เอาไปรันที่เครื่องไหนก็ได้

```
git init
git add .
git commit -m "DeFeedback VST3"
git remote add origin https://github.com/<user>/<repo>.git
git push -u origin main
```

### ทาง B: บิ้วในเครื่องตัวเอง

ต้องมี Visual Studio 2022, CMake และ [Inno Setup 6](https://jrsoftware.org/isdl.php)
แล้วดับเบิลคลิก `build-installer.bat` (JUCE โหลดเองอัตโนมัติครั้งแรก)

ได้ไฟล์ที่ `installer\Output\DeFeedback-Setup-0.2.0.exe`

ถ้าอยากได้แค่ปลั๊กอินเปล่า ๆ ไม่เอาตัวติดตั้ง:

```
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release --target DeFeedback_VST3
```

## ตัวติดตั้งทำอะไร

- ก๊อป bundle ไปที่ `C:\Program Files\Common Files\VST3\` (เปลี่ยนโฟลเดอร์ได้ในหน้า wizard)
- ถ้าเจอ DeFeedback ตัวเก่าอยู่คนละที่ จะถามก่อนลบ กันปลั๊กอินซ้อนกันสองที่
- ลงทะเบียนใน Add/Remove Programs ถอนออกได้ปกติ
- จบแล้วมีหน้าบอกวิธี rescan ในแต่ละโปรแกรม

ตัวติดตั้งยังไม่ได้เซ็น certificate ครั้งแรกที่รัน SmartScreen จะเตือน
กด **More info** > **Run anyway**

## ใช้งานแบบโปรแกรมเดี่ยว

เปิดจาก Start Menu > DeFeedback แล้วกด **Settings** ตั้งค่าอุปกรณ์

| ช่อง | ตั้งเป็น |
|---|---|
| Audio device type | ASIO ถ้ามี ไม่มีใช้ Windows Audio (Exclusive Mode) |
| Input | ไมค์ หรือการ์ดเสียงที่ต่อไมค์ |
| Output | ลำโพง หรือการ์ดเสียงที่ออกลำโพง |
| Sample rate | 48000 |
| Buffer size | 256 (หน่วงให้ลด สะดุดให้เพิ่ม) |

**เรื่อง latency** — ตัวที่ CI บิ้วให้ใช้ WASAPI/DirectSound เท่านั้น เพราะ ASIO SDK
ของ Steinberg แจกต่อไม่ได้ตามไลเซนส์ ถ้าต้องการ ASIO ให้โหลด SDK เองแล้วบิ้วในเครื่อง

```
cmake -B build -G "Visual Studio 17 2022" -A x64 -DASIO_SDK_DIR=C:/asiosdk
cmake --build build --config Release --target DeFeedback_Standalone
```

## โฮสต์ที่ใช้ได้ / ไม่ได้

| โฮสต์ | ใช้ได้ |
|---|---|
| Cubase / Nuendo | ได้ (อยู่หมวด EQ) |
| Reaper / Studio One / Ableton | ได้ |
| Waves SuperRack Performer V14+ | ได้ |
| **Waves MultiRack** | **ไม่ได้** รับเฉพาะฟอร์แมต SoundGrid |
| ไม่มีโฮสต์เลย | ใช้โปรแกรมเดี่ยว |

## ติดตั้งแบบมือ (ไม่ใช้ตัว setup)

ก๊อปโฟลเดอร์ `DeFeedback.vst3` ทั้งโฟลเดอร์ไปที่ `C:\Program Files\Common Files\VST3\`
แล้วสั่ง rescan ในโฮสต์

## การทดสอบ

`test/harness.cpp` จำลองสิ่งที่โฮสต์ทำจริง รันอัตโนมัติทุกครั้งที่ push
ถ้าเทสไม่ผ่าน CI จะหยุด ไม่ปล่อยตัวติดตั้งออกมา

รันเองบน Linux/Mac พร้อม AddressSanitizer:

```
cmake -B build -DDEFEEDBACK_BUILD_TESTS=ON
cmake --build build --target DeFeedbackTest
./build/test/DeFeedbackTest_artefacts/DeFeedbackTest
```

ครอบคลุม: เรียก processBlock ก่อน prepareToPlay, sampleRate เป็น 0/ติดลบ,
block size 1 ถึง 8192, buffer 0 ช่อง, สัญญาณหอนจริง 10 วินาที,
input ที่เป็น NaN/Inf/ค่ามหาศาล, เรียก prepare/release สลับกัน, state save/load

## บั๊กที่เคยเจอและแก้แล้ว

**v0.3.0** — Cubase พังตอนสแกนปลั๊กอิน AddressSanitizer ชี้ว่าเป็น SEGV เขียนลงที่อยู่ 0
ใน `processBlock` สาเหตุคือโฮสต์เรียก `processBlock` ก่อน `prepareToPlay` ตอนสแกน
ตอนนั้นบัฟเฟอร์ข้างในยังไม่ถูกจอง แก้ด้วยการ์ด `prepared` และตรวจขนาดบัฟเฟอร์ก่อนแตะ

**v0.3.0** — NaN ที่หลุดเข้ามาจะค้างใน state ของ IIR ตลอดไป ทำให้เสียงเงียบถาวร
แก้ด้วยการล้างค่า input ที่ไม่ใช่ตัวเลขปกติ และตรวจ state ฟิลเตอร์ท้ายทุก block

**v0.3.1** — ดับเบิลคลิก `.exe` แล้วเงียบ ไม่มีหน้าต่างขึ้น ไม่มี error
สาเหตุคือเครื่องไม่มี Visual C++ Redistributable แก้ด้วยการลิงก์ MSVC runtime แบบ static
(`CMAKE_MSVC_RUNTIME_LIBRARY`) ตอนนี้ CI มีขั้นตอนตรวจว่าไฟล์ที่ออกมาไม่อ้าง
`VCRUNTIME140.dll` / `MSVCP140.dll` อีก ถ้ายังอ้างอยู่จะไม่ปล่อยตัวติดตั้งออกมา

## หมายเหตุ

- latency = 0 ใช้ IIR ล้วน ไม่มี lookahead
- รองรับทั้งโมโนและสเตอริโอ
- ทดสอบแล้ว: ฟิลเตอร์เสถียรทุกช่วงพารามิเตอร์ (poles อยู่ในวงกลมหนึ่งหน่วยทั้ง 1840 ชุด), bell ที่ 0 dB โปร่งใสสนิท
- JUCE 8 ใช้ฟรีสำหรับใช้เอง/แจกแบบ open source (AGPL) ถ้าจะขายต้องซื้อไลเซนส์
