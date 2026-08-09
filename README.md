# Despertador Heavy Sleepers — v0.3.0-alpha

Despertador para quem tem sono pesado, com dois ESP32 conversando por UART.
Combina luz gradual, vibração, som crescente e desafios cognitivos na tela
touch: o alarme só desliga quando o desafio é resolvido.

```
heavy_sleeper_display/     ESP32 com tela — relógio, telas, desafios, áudio
heavy_sleeper_actuator/    ESP32 sem tela — PWM de luz e vibração
heavy_sleeper_demo/        demo estático anterior (v0.1), mantido como referência
WIRING.md                  esquema de ligação
```

---

## Antes de compilar

### 1. Biblioteca `XPT2046_Touchscreen` — **obrigatória**

Arduino IDE → Library Manager → instalar
**"XPT2046_Touchscreen" de Paul Stoffregen**. Sem ela o `heavy_sleeper_display`
não compila.

O touch do board E32R28T tem **barramento SPI próprio** (CLK=25, MOSI=32,
MISO=39, CS=33, IRQ=36), separado do SPI da tela. Por isso o firmware **não**
usa `tft.getTouch()` da TFT_eSPI, que assume barramento compartilhado. Ver
`WIRING.md` §2.2.

### 2. `TOUCH_CS` no `User_Setup.h` — **já aplicado**

A linha abaixo está em
`Documentos/Arduino/libraries/TFT_eSPI/User_Setup.h`, logo após `TFT_BL`:

```cpp
#define TOUCH_CS 33
```

Ela existe **apenas** para a `TFT_eSPI` compilar; o valor tem que bater com
`PIN_TOUCH_CS` do `config.h`. Quem lê o touch de verdade é a
`XPT2046_Touchscreen`, num `SPIClass(HSPI)` separado (a tela ocupa o VSPI).

### 3. Core ESP32 Arduino 3.x — atenção

A máquina tem o core **esp32 3.3.5**. Nele as funções `ledcSetup()` e
`ledcAttachPin()` **foram removidas** e `ledcWrite()` passou a receber o
**pino** em vez do canal. O código clássico

```cpp
ledcSetup(0, 5000, 8);      // não existe mais no core 3.x
ledcAttachPin(pino, 0);     // não existe mais no core 3.x
ledcWrite(0, valor);
```

não compila. Cada projeto tem um `pwm.h` que escolhe a API certa em tempo de
compilação, então o mesmo código roda no core 2.x e no 3.x:

```cpp
pwmAttach(pino, canal, freq, bits);   // canal só é usado no 2.x
pwmWrite(pino, canal, duty);
```

---

## Compilar e gravar

Pela Arduino IDE: abra cada pasta e selecione a placa **ESP32 Dev Module**.

Pela linha de comando (o `arduino-cli` vem junto com a Arduino IDE):

```bash
CLI="/c/Program Files/Arduino IDE/resources/app/lib/backend/resources/arduino-cli.exe"
LIBS="/c/Users/linco/OneDrive/Documentos/Arduino/libraries"

"$CLI" compile --fqbn esp32:esp32:esp32 --libraries "$LIBS" heavy_sleeper_display
"$CLI" compile --fqbn esp32:esp32:esp32 heavy_sleeper_actuator

# gravar (ajuste a porta)
"$CLI" upload -p COM3 --fqbn esp32:esp32:esp32 heavy_sleeper_display
```

Estado atual: os dois compilam sem avisos.
Display 28% de flash / 7% de RAM · Actuator 22% / 6%.

---

## Modo demo

`config.h` do display traz `#define DEMO_MODE` ligado:

- o relógio inicia em **07:29:00** e o alarme dispara em ~1 minuto;
- `DEMO_FAST_ESCALATE` encurta os estágios de 2/5/10 min para **20/45/80 s** e
  a soneca de 5 min para **30 s**, senão a apresentação levaria 10 minutos
  para chegar ao nível máximo.

Para uso real, comente `DEMO_MODE`. Para demo com os tempos verdadeiros,
comente só `DEMO_FAST_ESCALATE`.

Na tela **AJUSTES → TESTAR ALARME** o alarme dispara na hora, sem esperar.

---

## Navegação

```
RELÓGIO ──toque longo (1,5 s)──► AJUSTES ──► CONFIG. ALARME
   │                                   └──► AJUSTAR HORA
   ├──toque no cabeçalho "ALARME"──► CONFIG. ALARME
   └──toque na hora───────────────► AJUSTAR HORA

ALARME TOCANDO ──desafio correto──► RELÓGIO
               ──SONECA───────────► RELÓGIO (+5 min, nível +1)
```

Durante o alarme não há saída pela navegação: ou resolve, ou soneca.

---

## Como o alarme escalona

| Momento | Luz | Vibração | Som | Desafio |
|---|---|---|---|---|
| T=0 | 30% | pulsada | nível 1 (suave) | nível configurado |
| T=2 min | 60% | contínua | nível 2 | +1 nível |
| T=5 min | 100% | contínua | nível 3 | +1 nível, tela pisca |
| T=10 min | 100% | contínua | nível 4 (sweep) | `ALARM:ESCALATED` no log |

As porcentagens de luz são **escaladas pelo máximo configurado**: com a luz
em 50%, os estágios viram 15% → 30% → 50%. O despertar continua gradual sem
estourar o limite escolhido.

Modificadores de dificuldade, somados e limitados a EXTREMO:
`nível configurado + 1 por erro + 1 a partir do estágio 2 + 1 por soneca`.

---

## Desafios

| Tipo | Fácil | Médio | Difícil | Extremo |
|---|---|---|---|---|
| **Matemática** | 1 díg + 1 díg | 2 díg ± 1 díg | multiplicação | dupla operação |
| **Memória** | 3 cores | 5 cores | 7 cores | 9 cores (1,5 s cada) |
| **Sequência** | 4 crescente | 5 cresc/decresc | 6 embaralhados | 7, só os pares |
| **Reação** | toque na cor pedida | Stroop (palavra × cor) | ache o diferente | símbolo **e** cor únicos |

Na Reação os botões só aparecem depois de uma espera aleatória de 1,2–3 s;
tocar antes conta como erro. A janela de resposta está em
`REACTION_WINDOW_MS` (`challenges.cpp`): 2000/2000/1800/1600 ms.

---

## Protocolo UART (115200, 8N1)

Display → Actuator:

```
LIGHT:0 … LIGHT:100      intensidade da luz (sobe em rampa de 30 s)
VIB:ON | VIB:OFF | VIB:PULSE
STATUS?
ALARM:START | ALARM:STOP | ALARM:ESCALATED
```

Actuator → Display:

```
READY                        no boot
ACK:LIGHT:50 / ACK:VIB:ON    confirmação
STATUS:LIGHT:50,VIB:ON
NACK:INVALID                 comando desconhecido
PING?                        watchdog após 60 s sem comandos
```

A luz **sobe** em rampa de 30 s e **desce** na hora — apagar precisa ser
instantâneo. O painel da tela principal mostra `ATUADOR ONLINE/OFFLINE`
conforme as respostas chegam (3 s de tolerância).

---

## Decisões de implementação

- **Nenhum `delay()` no display.** Tudo é `millis()` e máquina de estados.
  O único ponto bloqueante é a task de áudio, isolada no **Core 0**; a UI
  fica no Core 1.
- A task de áudio cede a CPU (`vTaskDelay(1)`) a cada ~100 ms de tom. Sem
  isso a IDLE0 morre de fome e o task watchdog reinicia o ESP32.
- O backlight só é anexado ao LEDC **depois** de `tft.init()`, porque a
  `TFT_eSPI` configura o pino 21 como saída digital durante o init.
- Layout e área clicável saem da **mesma tabela de botões** (`curBtns[]` em
  `ui.cpp`), então nunca saem de sincronia.
- Preferences corrompida (valores fora de faixa) volta sozinha para os
  padrões: alarme 07:00, nível Fácil, luz 50%.

---

## Pendências conhecidas

- Nada foi testado em hardware ainda — só compilado. A calibração do touch,
  o volume real do DAC e o comportamento dos MOSFETs precisam de bancada.
- **Touch:** a calibração de 3 pontos detecta sozinha se os eixos brutos
  estão trocados ou invertidos. Se mesmo assim o toque cair no lugar errado,
  ajuste `touch.setRotation()` em `touch.cpp` (0–3) e recalibre — apagar a
  chave `touch_cal` da NVS força a rotina no próximo boot.
- **Áudio:** se não sair som, confirme o `PIN_AMP_ENABLE` (IO4) indo a **LOW**
  durante o alarme — é ele que habilita o amplificador do board.
- **LEDs Verde e Vermelho piscam junto com a UART** (IO16/IO22). É esperado,
  não é defeito: ver o aviso em `WIRING.md` §2.5.
- O relógio é por software (`millis()`): perde a hora ao reiniciar e vai
  derivando. Para produto real, entra um DS3231 ou NTP por Wi-Fi.
