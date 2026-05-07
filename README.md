# Smart Baby Cry Alert System 🍼

An IoT system that detects baby crying and instantly alerts parents in another room — no internet or phone required.

## How It Works
- **ESP32 #1 (Baby's Room):** Listens via INMP441 microphone and analyzes audio using FFT
- **ESP32 #2 (Parent's Room):** Receives wireless alert and triggers buzzer + TFT display
- Communication via **ESP-NOW** protocol (direct, ~1ms latency)

## Detection Algorithm
Three conditions must all pass simultaneously:
1. **RMS level** — sound must be loud enough
2. **Frequency analysis** — dominant frequency must fall in baby cry range (300–800 Hz)
3. **Pitch variation** — frequency must wobble over time (cries vary, music doesn't)

## Hardware
| Component | Purpose |
|---|---|
| 2x ESP32 | Main microcontrollers |
| INMP441 | Digital I2S microphone |
| ST7789 1.54" TFT | Alert display |
| Active buzzer | Audio alert |
| Li-ion battery | Portable power |
| Tactile push button | Silence buzzer |

## Files
- `ESP1_Sender.ino` — Microphone + cry detection + wireless sender
- `ESP2_Receiver.ino` — Display + buzzer + wireless receiver

## Built With
Arduino IDE · C++ · ESP-NOW · I2S · arduinoFFT

## Team
 Seham Nasr Elmaghraby · Farah Mohamed Ghareib · Mariam Mohamed Bayoumi 

**Supervised by:** Dr. Mohamed Maher · TA. Mohamed Kamaal · TA. Tasneem Gamaal
