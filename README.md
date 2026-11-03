# Despertador Heavy Sleepers — v0.4.0-alpha

Despertador para quem tem sono pesado, com dois ESP32 conversando por UART.
Combina luz gradual (lâmpada Wi-Fi WiZ), vibração, som crescente e desafios
cognitivos na tela touch: o alarme só termina quando o desafio é resolvido.

```
heavy_sleeper_display/     ESP32 com tela — relógio, telas, desafios, áudio,
                           Wi-Fi, portal web e lâmpada WiZ
heavy_sleeper_actuator/    ESP32 sem tela — PWM da vibração (luz reservada)
heavy_sleeper_demo/        demo estático anterior (v0.1), mantido como referência
tests/host/                simulação do alarme no PC (g++), sem hardware
WIRING.md                  esquema de ligação
```

## Novidades da v0.4

1. **Lâmpada Wi-Fi WiZ (Philips)** no lugar da luz por MOSFET. Ela começa a
   acender *antes* do alarme, como um nascer do sol: de 10% e luz quente
   (2200 K) até o brilho máximo e luz branca (6500 K) na hora do alarme.
2. **Novo fluxo do alarme:** tocou → você **segura DESLIGAR por 2 s** → o som
   para e começa o **desafio cronometrado** → errou ou acabou o tempo, toca
   de novo. Não é mais preciso estar olhando a tela no segundo em que o
   alarme dispara.
3. **Modo noturno:** à noite a tela mostra só a hora, grande, em vermelho e
   com brilho mínimo. Automático por horário (22h–6h por padrão), ou
   sempre dia / sempre noite.
4. **Portal de configuração pelo celular**, hora certa pela internet (NTP) e
   um teste no PC que simula uma manhã inteira de alarme.

---

## Antes de compilar

### 0. Nada novo para instalar

Wi-Fi, servidor web, DNS do portal e mDNS (`WiFi`, `WebServer`, `DNSServer`,
`ESPmDNS`) já vêm no core ESP32. A lâmpada WiZ usa UDP puro, sem biblioteca.

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

Estado atual: os dois compilam sem avisos (core esp32 3.3.5).
Display **84%** de flash / 16% de RAM (o Wi-Fi pesa) · Actuator 22% / 6%.

> Ainda cabe no esquema padrão de partições, mas sobra pouco. Se uma versão
> futura passar de 100%, em **Ferramentas → Partition Scheme** escolha
> **"Huge APP (3MB No OTA/1MB SPIFFS)"** (no `arduino-cli`:
> `--fqbn esp32:esp32:esp32:PartitionScheme=huge_app`).

### Teste no PC (sem placa)

```bash
tests/host/run.sh
```

Compila a lógica real do display com tela, touch, Wi-Fi e lâmpada falsos e
simula: rampa da luz, tocar, segurar DESLIGAR, errar, tempo esgotado,
escalada, acerto, soneca e modo noturno — para os 4 tipos de desafio.

---

## Modo demo

`config.h` do display traz `#define DEMO_MODE` ligado:

- o relógio inicia em **07:29:00** e o alarme dispara em ~1 minuto;
- a hora **não** é sobrescrita pelo NTP, mesmo com Wi-Fi;
- `DEMO_FAST_ESCALATE` encurta os estágios de 2/5/10 min para **20/45/80 s**,
  a soneca de 5 min para **30 s** e a rampa da lâmpada para **45 s** (começa
  às 07:29:15), senão a apresentação levaria 10 minutos.

Para uso real, comente `DEMO_MODE`. Para demo com os tempos verdadeiros,
comente só `DEMO_FAST_ESCALATE`.

Na tela **AJUSTES → TESTAR ALARME** o alarme dispara na hora, sem esperar.

---

## Primeiro uso: Wi-Fi pelo celular

1. Ligue o display. Sem rede salva ele abre o portal: no celular, conecte na
   rede **`Despertador-XXXX`**, senha **`despertador`**.
2. A página de configuração abre sozinha (ou acesse `http://192.168.4.1`).
3. Em **Rede Wi-Fi**: *Procurar redes* → *Usar* → senha → *Conectar*.
4. A tela **AJUSTES → REDE E LÂMPADA** mostra o IP. Daí em diante o portal
   fica em **`http://despertador.local`** (ou no IP) pela rede de casa.

Se a rede de casa cair ou a senha estiver errada, o portal volta a abrir
sozinho depois de 20 s e o despertador tenta reconectar a cada minuto
(só quando ninguém está usando o portal: o ESP32 tem um rádio só, e a
busca derrubaria o celular conectado). O botão **ABRIR PORTAL** da tela REDE
abre o ponto de acesso por 10 min mesmo com Wi-Fi conectado.

O portal configura alarme, desafio, tempo para resolver, rampa e brilho da
lâmpada, IP da lâmpada, modo noturno e fuso horário. **Enquanto o alarme
toca, ele recusa mudanças no alarme**, para não virar um atalho para fugir
do desafio.

## Lâmpada WiZ

- Comunicação **local** por UDP (porta 38899), sem nuvem nem conta. Se a
  lâmpada não responder, ligue a opção de comunicação local no app WiZ.
- **IP automático** (padrão): o display procura a lâmpada por broadcast ao
  conectar e de novo se ela sumir. Com mais de uma lâmpada na casa, escolha
  a certa no portal (*Procurar* → *Usar* → *Salvar*), o que fixa o IP.
  Reservar o IP dela no roteador evita que ele mude.
- **Rampa** de 5/10/15/20 min (padrão 10): começa em 10% / 2200 K e chega ao
  **brilho máximo** configurado / 6500 K na hora do alarme. Passo a cada 3 s.
- Desafio resolvido → a lâmpada **continua acesa**. Soneca → **apaga**.
  Alarme desativado ou horário mudado no meio da rampa → apaga.
- Lâmpadas **branco ajustável** só aceitam 2700–6500 K. Se a rampa não
  começar, troque `WIZ_TEMP_MIN` para 2700 no `config.h`.

## Navegação

```
RELÓGIO ──toque longo (1,5 s)──► AJUSTES ──► CONFIG. ALARME
   │                                   ├──► MODO NOTURNO
   │                                   ├──► REDE E LÂMPADA
   │                                   ├──► AJUSTAR HORA
   │                                   └──► TESTAR ALARME
   ├──toque no cabeçalho "ALARME"──► CONFIG. ALARME
   ├──toque na hora───────────────► AJUSTAR HORA
   └──(modo noturno) toque ───────► mostra a tela completa por 10 s

ALARME TOCANDO ──segurar DESLIGAR 2 s──► DESAFIO (silêncio, cronometrado)
               ──SONECA───────────────► RELÓGIO (+5 min, nível +1, luz apaga)

DESAFIO ──acertou──────────────────► RELÓGIO (luz continua acesa)
        ──errou / tempo esgotado──► ALARME TOCANDO (nível +1)
```

Telas de ajuste esquecidas abertas voltam sozinhas ao relógio depois de
1 min sem toque (e o modo noturno volta junto).

---

## Como o alarme escalona

| Tempo **tocando** | Luz (WiZ) | Vibração | Som | Desafio |
|---|---|---|---|---|
| T=0 | brilho máximo | pulsada | nível 1 (suave) | nível configurado |
| T=2 min | brilho máximo | contínua | nível 2 | +1 nível |
| T=5 min | brilho máximo | contínua | nível 3 | +1 nível, tela pisca |
| T=10 min | brilho máximo | contínua | nível 4 (sweep) | `ALARM:ESCALATED` no log |

O relógio da escalada **só anda enquanto o alarme toca**: o tempo gasto
resolvendo o desafio não conta. Errar e voltar a tocar retoma do estágio em
que estava.

Modificadores de dificuldade, somados e limitados a EXTREMO:
`nível configurado + 1 por erro (ou tempo esgotado) + 1 a partir do estágio 2 + 1 por soneca`.

Tempo para resolver: 30/60/90/120 s (padrão 60), contado a partir do
momento em que o desafio aparece — inclui a exibição da sequência na
Memória. Na **Reação**, a janela curta da própria reação (abaixo) estoura
antes: não tocar a tempo conta como erro e o alarme volta.

## Modo noturno

| Modo | Comportamento |
|---|---|
| AUTO (padrão) | noturno entre **22:00 e 06:00** (ajustável de 30 em 30 min) |
| DIA | tema normal o tempo todo |
| NOITE | noturno o tempo todo |

No noturno a tela do relógio mostra só a hora (horas e minutos empilhados,
7 segmentos em escala 2) em vermelho, com o backlight em ~2% (`BL_NIGHT`).
Um toque mostra a tela completa por 10 s; o toque longo continua abrindo
os ajustes. O alarme sempre aparece com a tela clara e brilho máximo.

**Sensor de luz (futuro):** descomente `PIN_LDR` no `config.h` e ligue um LDR
em divisor de tensão no **IO35** (ADC1, funciona com Wi-Fi; os pinos ADC2
não funcionam). No modo AUTO a tela passa a seguir a luz do ambiente
(`LDR_DARK_ADC` com histerese).

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
VIB:ON | VIB:OFF | VIB:PULSE
STATUS?
ALARM:START | DISMISS | RESUME | STOP | SNOOZE | ESCALATED   (só log)
LIGHT:0 … LIGHT:100      RESERVADO: só com USE_MOSFET_LIGHT nas duas placas
```

Actuator → Display:

```
READY                        no boot
ACK:LIGHT:50 / ACK:VIB:ON    confirmação
STATUS:LIGHT:50,VIB:ON
NACK:INVALID                 comando desconhecido
NACK:LIGHT_DISABLED          LIGHT: com a luz por MOSFET desativada
PING?                        watchdog após 60 s sem comandos
```

**Luz por MOSFET reservada:** desde a v0.4 a luz é a lâmpada WiZ. O código
da luz por PWM no GPIO32 continua no Actuator, desligado. Para testes,
descomente `#define USE_MOSFET_LIGHT` nos **dois** `config.h`: o display
volta a mandar `LIGHT:` (30/60/100% por estágio) junto com a WiZ.

O painel da tela principal mostra `ATUADOR ONLINE/OFFLINE` conforme as
respostas chegam (3 s de tolerância), além de Wi-Fi e lâmpada.

## Portal — API

Para criar opções novas no futuro: um campo no `<form id="cfg">` de
`portal_page.h` e a mesma chave em `handleSave()` (`portal.cpp`).

```
GET  /                    página
GET  /api/state           estado completo (JSON, sem a senha do Wi-Fi)
POST /api/save            almTime, almEn, type, diff, chal, vib, ramp, light,
                          wizIp, nightMode, nightStart, nightEnd, tz
GET  /api/scan            redes Wi-Fi (assíncrono: repita até scanning=false)
POST /api/wifi            ssid, pass → grava e reconecta
POST /api/wifi/forget     apaga a rede e abre o portal
POST /api/wiz/discover    procura lâmpadas
POST /api/wiz/test        pisca a lâmpada
```

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
- **O LED Vermelho pisca junto com a UART** (IO22, que é o RX2). É
  esperado, não é defeito: ver o aviso em `WIRING.md` §2.5. Desde a v0.3 o
  TX2 é o **IO27**, então o LED Verde (IO16) não pisca mais.
- **Relógio:** com Wi-Fi a hora vem do NTP (`a.st1.ntp.br`) e é corrigida a
  cada 30 s. Sem internet continua por software e perde a hora ao
  reiniciar; para funcionar sem Wi-Fi, ainda vale um DS3231.
- **Novo na v0.4, a conferir na bancada:** resposta real da lâmpada WiZ
  (faixa de temperatura, comunicação local no app); som com o Wi-Fi ligado
  (o rádio também usa o Core 0, onde roda a task de áudio, e pode haver
  pequenos cortes); se segurar DESLIGAR por 2 s é confortável no touch
  resistivo (uma falha de leitura de até 150 ms não zera a contagem); se o
  vermelho e o brilho do modo noturno incomodam no quarto escuro
  (`C_NIGHT`, `BL_NIGHT`).
- O portal não tem login: qualquer pessoa na mesma rede consegue abrir.
  Aceitável para o protótipo; para produto, entra uma senha.
