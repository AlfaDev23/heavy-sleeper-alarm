# WIRING — Despertador Heavy Sleepers

Esquema de ligação dos dois ESP32, dos MOSFETs de luz e vibração e do áudio.

> **v0.4:** a luz do despertar agora é uma **lâmpada Wi-Fi WiZ**, comandada
> pelo Display pela rede — não há fio. O MOSFET de luz (GPIO32 do Actuator)
> ficou **reservado** (`USE_MOSFET_LIGHT` comentado); pode ficar sem montar.

> **Regra que não pode ser esquecida:** os dois ESP32 e todas as fontes
> precisam compartilhar o **GND**. Sem terra comum a UART não funciona e o
> gate do MOSFET flutua.

---

## 1. Visão geral

```
                    ┌────────────────────────────┐
                    │  DISPLAY — board E32R28T   │
                    │  ESP32-32E + ST7789 + touch│
                    │                            │
   speaker 8R ◄─────┤ conector SPEAKER do board  │
                    │  (DAC IO26 + amp IO4)      │
                    │                            │
                    │ GPIO16 (TX2) ──────────────┼──┐
                    │ GPIO22 (RX2) ◄─────────────┼─┐│
                    │ GND ───────────────────────┼┐││
                    └────────────────────────────┘│││
                                                  │││
                    ┌────────────────────────────┐│││
                    │   ACTUATOR ESP32 (30 pinos)│││││
                    │                            ││││
                    │ GPIO16 (RX2) ◄─────────────┼┼┼┘   TX -> RX
                    │ GPIO17 (TX2) ──────────────┼┼┘    TX -> RX
                    │ GND ───────────────────────┼┘     terra comum
                    │                            │
                    │ GPIO32 ──[220R]──► gate MOSFET LUZ (reservado)
                    │ GPIO33 ──[220R]──► gate MOSFET VIBRAÇÃO
                    │ GPIO2  ──► LED azul onboard (status)
                    └────────────────────────────┘
```

---

## 2. Display ESP32 (board E32R28T) — pinos

> Esta seção é a **verdade do datasheet oficial** do board (LCDWIKI /
> QDtech). Nenhum número de pino deve ser inventado ou hardcoded fora do
> `config.h`.

### 2.1 Tela ST7789 (já fixos no `User_Setup.h`, **não mexer**)

| Sinal | GPIO |
|---|---|
| TFT_MISO | 12 |
| TFT_MOSI | 13 |
| TFT_SCLK | 14 |
| TFT_CS | 15 |
| TFT_DC | 2 |
| TFT_RST | −1 (não ligado) |
| TFT_BL | 21 (backlight, PWM via LEDC) |

SPI a 40 MHz no barramento **VSPI**, driver ST7789, 240×320, rotação 0.

### 2.2 Touch XPT2046 — **barramento SPI próprio**

| Sinal | GPIO | Observação |
|---|---|---|
| TOUCH_CS | 33 | precisa bater com `TOUCH_CS` do `User_Setup.h` |
| TOUCH_CLK | 25 | **não é** DAC de áudio |
| TOUCH_MOSI | 32 | |
| TOUCH_MISO | 39 | somente input |
| TOUCH_IRQ | 36 | somente input |

> **O touch NÃO compartilha o SPI da tela.** Por isso o firmware não usa
> `tft.getTouch()` nem `tft.calibrateTouch()` — a TFT_eSPI assume barramento
> compartilhado e não funciona aqui. Quem lê o touch é a biblioteca
> **XPT2046_Touchscreen** (Paul Stoffregen), num `SPIClass(HSPI)` separado,
> já que o **VSPI** está ocupado pela tela. O `#define TOUCH_CS 33` do
> `User_Setup.h` existe só para a TFT_eSPI compilar.

### 2.3 Áudio

| Função | GPIO | Observação |
|---|---|---|
| DAC de áudio | 26 | DAC2, saída analógica para o amplificador |
| AMP_ENABLE | 4 | **LOW = amplificador ligado**, HIGH = mudo |

### 2.4 SD card / SPI periférico (não usado pelo firmware)

| Função | GPIO |
|---|---|
| CS_SD | 5 |
| CS_SPI | 27 |
| CLK | 18 |
| MOSI | 23 |
| MISO | 19 |

### 2.5 UART2 e LED RGB — **pinos compartilhados**

| Função | GPIO | Observação |
|---|---|---|
| UART2 TX | 16 | vai para o **RX (16)** do Actuator — é o LED **Verde** |
| UART2 RX | 22 | vem do **TX (17)** do Actuator — é o LED **Vermelho** |
| LED de status do link | 17 | LED **Azul**, livre |

> ⚠️ **Aviso — UART e LED RGB dividem os mesmos pinos.**
> No E32R28T o LED RGB (common anode, LOW acende) está em
> **IO22 = Vermelho, IO16 = Verde, IO17 = Azul**. Como IO16 e IO22 foram
> escolhidos para a Serial2, os LEDs Vermelho e Verde **piscam junto com o
> tráfego da UART**. Isso é uma decisão consciente do projeto, não um bug —
> mas significa que **nada no firmware pode acionar IO16 ou IO22 como LED**.
> Só o **LED Azul (IO17)** sobra, e o `comm.cpp` o usa como indicador:
> apagado = link OK, piscada curta = linha recebida, piscar rápido =
> Actuator offline.

### 2.6 Pinos que **não** podem virar saída

`IO34` (ADC de bateria), `IO35` (pino de expansão), `IO36` (touch IRQ),
`IO39` (touch MISO) — são **input only** no ESP32.

**Sensor de luz (opcional, futuro):** o `IO35` está reservado para um LDR do
modo noturno automático (`PIN_LDR` no `config.h`). Ele é ADC1, que continua
funcionando com o Wi-Fi ligado — os pinos ADC2 (IO0, 2, 4, 12–15, 25–27)
**não** leem analógico com o Wi-Fi ativo.

```
3V3 ──[LDR]──┬──[10k]── GND
             └────────► IO35
```

Mais luz → menor resistência do LDR → tensão maior no IO35.

### 2.7 Pinos reservados / perigosos

`IO0` (botão BOOT), `IO1` (TX0) e `IO3` (RX0) — o conector UART físico do
board usa IO1/IO3; não reutilize.

---

## 3. Actuator ESP32 — pinos

| Função | GPIO | Observação |
|---|---|---|
| UART2 RX | 16 | vem do **TX (17)** do Display |
| UART2 TX | 17 | vai para o **RX (16)** do Display |
| PWM luz | 32 | **reservado** (5 kHz, 8 bits, só com `USE_MOSFET_LIGHT`) |
| PWM vibração | 33 | 200 Hz, 8 bits |
| LED status | 2 | LED azul onboard |

GPIO 32 e 33 foram escolhidos porque **não são pinos de strapping** e ficam
em alta impedância no boot — o MOSFET não dispara sozinho ao ligar.

---

## 4. Link UART entre as duas placas

```
DISPLAY GPIO16 (TX2) ─────────────► ACTUATOR GPIO16 (RX2)
DISPLAY GPIO22 (RX2) ◄───────────── ACTUATOR GPIO17 (TX2)
DISPLAY GND         ──────────────  ACTUATOR GND
```

> Os pinos do **Display** mudaram na v0.3 (eram 17/16). Os do **Actuator**
> continuam 16/17 — aquela placa é um ESP32 devkit comum, sem LED RGB nesses
> pinos.

- **TX de um vai no RX do outro** (cruzado). TX↔TX não comunica.
- 115200 8N1.
- **Não ligue os 3V3 das duas placas entre si** se cada uma tem sua própria
  alimentação USB. Só o GND é compartilhado.
- Ambos são 3,3 V — não precisa de level shifter.

---

## 5. MOSFETs — luz e vibração

Chaveamento **low-side** com N-channel de **nível lógico** (o gate precisa
saturar com 3,3 V). Servem: **IRLZ44N**, **IRL540N**, **AOD4184**.
Um IRF540 comum **não serve** — ele não satura com 3,3 V no gate.

```
                    +Vcc (12 V da fita de LED / 5 V do motor)
                       │
                     [CARGA]        ← fita de LED  ou  motor de vibração
                       │
                       ├──────┤◄──┐  diodo flyback (só na vibração)
                       │          │  1N5819 / 1N4007, catodo para +Vcc
                       │          │
                       │◄─────────┘
                    ┌──┴──┐
GPIO32 ──[220R]─────┤ G   │ DRAIN
  (ou 33)           │  MOSFET N logic-level
                    └──┬──┘ SOURCE
                       │
        [10k] ─────────┤     ← pulldown gate→source
                       │
                      GND  ← MESMO GND do ESP32 e da fonte
```

Componentes por canal:

| Item | Valor | Para quê |
|---|---|---|
| Resistor de gate | 100–470 Ω (use 220 Ω) | limita o pico de corrente no gate |
| Pulldown de gate | 10 kΩ (gate→GND) | mantém desligado no boot/reset |
| Diodo flyback | 1N5819 ou 1N4007 | **obrigatório no motor**; catodo no +Vcc |

Notas:

- O diodo flyback é **indispensável** na vibração. Motor é carga indutiva; sem
  o diodo o pico de tensão no desligamento destrói o MOSFET.
- Na fita de LED o diodo não é necessário.
- **Não alimente a fita de LED nem o motor pelo pino 3V3 ou VIN do ESP32.**
  Use fonte externa e junte apenas o GND.
- Dissipador no MOSFET se a fita passar de ~2 A.

---

## 6. Áudio — GPIO26 (DAC) + GPIO4 (enable do amplificador)

> ⚠️ **Não é o GPIO25.** No E32R28T o IO25 é o **CLK do touch**. O DAC de
> áudio é o **IO26** (DAC2).

O board já traz um **amplificador ligado ao conector SPEAKER**, alimentado
pelo DAC IO26. Esse amplificador tem um pino de habilitação:

| Pino | Nível | Efeito |
|---|---|---|
| IO4 (`PIN_AMP_ENABLE`) | **LOW** | amplificador ligado |
| IO4 | **HIGH** | mudo |

**Sem acionar o IO4 não sai som algum pelo alto-falante do board.** O
firmware sobe com o amp em HIGH (mudo) e só o habilita dentro da task de
áudio, enquanto há tom sendo gerado (`audioEnable()` / `audioDisable()` em
`audio.cpp`).

### Ligação

```
conector SPEAKER do board ──► speaker 8R
```

É só plugar o alto-falante no conector do board. O DAC e o amplificador já
estão roteados internamente.

### Alternativa — speaker direto no GPIO (sem o amp do board)

O IO26 é um DAC de verdade (0–3,3 V), mas praticamente **sem corrente**:
ligar um alto-falante direto nele não funciona e ainda arrisca o pino.

```
GPIO26 ──[ 10uF ]──[ 100R ]──► speaker 8R ──► GND
          (+ no lado do GPIO26)
```

O capacitor tira o nível DC (o código deixa o DAC em 128 = meia escala no
silêncio) e o resistor limita a corrente. Volume baixo, só para bancada.
Ou um módulo **PAM8403** / **LM386**: GPIO26 → capacitor 10 µF → entrada do
amp, GND comum, alto-falante na saída do amp.

O volume no firmware é a **amplitude do DAC** (`LEVEL_AMP` em `audio.cpp`),
que sobe junto com os estágios do alarme.

---

## 7. Ordem de montagem sugerida

1. Só o Display ESP32, sem nada ligado: confirmar tela e touch.
2. Ligar os três fios da UART (TX, RX, GND) e confirmar `READY` / `ONLINE`
   no painel da tela principal.
3. Só depois montar os MOSFETs, **testando primeiro com um LED comum +
   resistor 1 kΩ no lugar da carga real**.
4. Por último trocar o LED de teste pela fita e pelo motor.

## 8. Checklist antes de ligar a fonte

- [ ] GND comum entre os dois ESP32 e a fonte externa
- [ ] TX↔RX cruzados: Display **16** → Actuator **16**, Actuator **17** →
      Display **22** (não TX↔TX)
- [ ] `TOUCH_CS` = **33** batendo no `User_Setup.h` e no `config.h`
- [ ] Biblioteca **XPT2046_Touchscreen** (Paul Stoffregen) instalada
- [ ] Pulldown de 10 kΩ em cada gate
- [ ] Diodo flyback no motor, catodo no +Vcc
- [ ] MOSFET é logic-level (IRLZ44N, não IRF540)
- [ ] Fita/motor **não** alimentados pelo ESP32
