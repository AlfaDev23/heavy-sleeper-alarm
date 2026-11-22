# Despertador Heavy Sleepers — v0.5.1-alpha

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

## Novidades da v0.5

1. **Vários alarmes** (até 6), cada um com horário, ligado/desligado e
   **dias da semana**: segunda a sexta, todos os dias, fim de semana, dias
   avulsos ou **uma vez só** (toca e se desativa).
2. **Tempo máximo tocando:** se ninguém desligar, o alarme desiste sozinho
   depois de 5/10/15/20/30 min (padrão 10) ou nunca. Som, vibração e luz
   apagam e o relógio mostra "Alarme das 07:00 não atendido".
3. O relógio agora sabe o **dia da semana** (pela internet, ou acertado em
   AJUSTAR HORA).
4. **Volume do alarme no portal** (v0.5.1): volume inicial e máximo, com
   botão para ouvir antes de salvar.

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

### 2b. Cores da tela no `User_Setup.h` — **já aplicado**

O padrão da `TFT_eSPI` para o ST7789 é inversão ligada e ordem RGB. O painel
do E32R28T precisa do contrário nas duas coisas, então estas linhas estão
**ativas** no mesmo `User_Setup.h`:

```cpp
#define TFT_RGB_ORDER TFT_BGR    // sem isso vermelho e azul trocam
#define TFT_INVERSION_OFF        // sem isso o preto vira branco
```

Sem elas o fundo fica branco (fraco no modo noturno) e o vermelho do modo
noturno aparece **amarelo** (azul por causa da troca, invertido = amarelo).
Conferência rápida: no modo noturno o fundo é preto e os números são
vermelhos; a faixa "!!! ALARME !!!" é vermelha e o botão DESLIGAR, verde.

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

Precisa de `g++` e `bash`. Compila a lógica real do display com tela, touch,
Wi-Fi e lâmpada falsos e simula: rampa da luz, tocar, segurar DESLIGAR, errar, tempo esgotado,
escalada, acerto, soneca, modo noturno e a troca demo ↔ produção — para os
4 tipos de desafio.

---

## Modo demo × produção (sem recompilar)

Desde a v0.4.1 o modo é trocado **com a placa montada**, sem recompilar:

- **Tela:** toque longo no relógio → **AJUSTES** → botão **MODO: PRODUÇÃO /
  MODO: DEMO** (verde = produção, laranja = demo).
- **Portal:** seção **Modo de operação** → *Mudar para DEMO / PRODUÇÃO*.

A escolha fica gravada na NVS e sobrevive a reinícios. O padrão de fábrica é
**produção** (`DEMO_DEFAULT` no `config.h`).

| | Produção (uso real) | Demo (apresentação) |
|---|---|---|
| Relógio | NTP (ou AJUSTAR HORA sem internet) | começa em **07:29:00**, NTP ignorado |
| Alarmes | a sua lista | só um, **07:30** todo dia |
| Escalada | 2 / 5 / 10 min | 20 / 45 / 80 s |
| Soneca | 5 min | 30 s |
| Rampa da lâmpada | 5–20 min (configurável) | 45 s (começa às 07:29:15) |

- A **lista de verdade não se perde**: no demo o alarme 07:30 fica só na
  RAM e salvar não grava o alarme. Ao voltar para produção ele é restaurado.
- Ao voltar para produção a hora vem do NTP no segundo seguinte. Sem
  internet o relógio volta a 00:00: acerte em **AJUSTES → AJUSTAR HORA**.
- A troca é recusada enquanto o alarme toca ou o desafio está na tela.
- No demo a tela do relógio mostra **MODO DEMO** no rodapé.
- Para um demo com os tempos reais, use `DEMO_FAST_TIMES 0` no `config.h`.

Na tela **AJUSTES → TESTAR ALARME** o alarme dispara na hora, nos dois modos.

---

## Primeiro uso: Wi-Fi pelo celular

1. Ligue o display. Sem rede salva ele abre o portal: no celular, conecte na
   rede **`Despertador-XXXX`**, senha **`despertador`**. `XXXX` são os 2
   últimos bytes do MAC do ponto de acesso, então o nome não muda entre boots.
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

## Alarmes

Até **6 alarmes**. Em cada um: horário, ligado/desligado e os dias.

| Dias | Toca |
|---|---|
| SEG-SEX | segunda a sexta |
| TODOS | todos os dias |
| dias avulsos (D S T Q Q S S) | só nos dias marcados |
| nenhum dia (UMA VEZ) | no próximo horário, e depois se desativa sozinho |

- **Na tela:** toque longo → AJUSTES → **ALARMES** (ou toque no cabeçalho
  "ALARME" do relógio). A lista mostra cada alarme com o botão ON/OFF; tocar
  no alarme abre a edição (hora, dias, ativo, SALVAR / EXCLUIR). **+ NOVO**
  cria um alarme; **OPÇÕES** abre as opções do despertar (desafio, luz,
  vibração, rampa, tempo para resolver e tempo máximo tocando), que valem
  para todos os alarmes.
- **No portal:** seção **Alarmes**, com *+ Adicionar*, horário, *Ativo*, os
  dias para tocar e *Salvar alarmes*. O status mostra o **próximo alarme**.
- A tela do relógio mostra o próximo alarme, com o dia da semana quando não
  é hoje (ex. `Alarme SEG 06:30  luz 06:20`), e a rampa da lâmpada acende
  para ele.
- **Dia da semana:** com internet vem do NTP. Sem internet, acerte em
  **AJUSTAR HORA → DIA** junto com a hora; se ele estiver errado, os alarmes
  de dias específicos tocam no dia errado.
- Quem vem da v0.4 tem o alarme antigo convertido no primeiro da lista,
  tocando todos os dias, como antes.

### Tempo máximo tocando

**OPÇÕES → TOCAR NO MÁXIMO** (ou *Tocar no máximo* no portal): 5, 10, 15,
20, 30 min ou sem limite. O padrão é **10 min**, como no celular. Conta só o
tempo **tocando**: o tempo resolvendo o desafio não entra, então errar o
desafio de propósito não encurta o alarme. Ao estourar, som, vibração e
lâmpada apagam (`ALARM:TIMEOUT` no log) e o relógio mostra **"Alarme das
07:00 não atendido"** em vermelho até o próximo toque na tela. No demo com
tempos curtos o limite é 2 min.

> Para quem tem sono muito pesado, "sem limite" é a opção mais segura: o
> limite existe para quando o alarme toca com a casa vazia.

## Navegação

```
RELÓGIO ──toque longo (1,5 s)──► AJUSTES ──► ALARMES ──► EDITAR ALARME
   │                                   │            └──► OPÇÕES DO DESPERTAR
   │                                   ├──► MODO NOTURNO
   │                                   ├──► REDE E LÂMPADA
   │                                   ├──► AJUSTAR HORA
   │                                   └──► TESTAR ALARME
   ├──toque no cabeçalho "ALARME"──► ALARMES
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
| T=0 | brilho máximo | fraca, pulsada | nível 1 (suave) | nível configurado |
| T=2 min | brilho máximo | média, contínua | nível 2 | +1 nível |
| T=5 min | brilho máximo | máxima, contínua | nível 3 | +1 nível, tela pisca |
| T=10 min | brilho máximo | máxima, contínua | nível 4 (sweep) | `ALARM:ESCALATED` no log |

A vibração nunca passa do nível escolhido em **Vibração** (tela ou portal):
*Fraca* fica só nos pulsos fracos, *Média* para na média e *Máxima* vai até
o fim.

### Volume do alarme

O som começa no **volume inicial** e sobe até o **volume máximo** conforme o
alarme escala; os níveis 2 e 3 ficam no meio do caminho. Ajuste no portal,
seção **Volume do alarme** (5 a 100%, padrão **25% / 90%**, o mesmo som das
versões anteriores). O botão **Testar** toca 2 s a melodia daquele momento
(a do início é mais grave, a do fim é um sweep agudo) no volume do
controle, **sem salvar**; depois, *Salvar configurações*. O máximo não pode
ficar abaixo do inicial. Acima de ~90% o alto-falante do board pode
distorcer. O teste é recusado com o alarme tocando.

### Intensidade da vibração (PWM)

O PWM de cada nível depende do motor, então é ajustado **no portal**, seção
**Intensidade da vibração**: um controle de 0 a 100% para Fraca, Média e
Máxima (padrão 45 / 70 / 100%). O botão **Testar** ao lado de cada um vibra
2 s naquela intensidade **sem salvar**, para achar o ponto testando; depois,
**Salvar configurações**. Se o motor não gira num valor baixo, suba esse
nível. O teste é recusado com o alarme tocando ou o atuador offline.

O Display manda `VIB:DUTY:<n>` antes de cada `VIB:ON`/`VIB:PULSE`. Precisa
do **Actuator 0.4.1**: o antigo responde `NACK:INVALID` e vibra sempre a 80%.

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

**Vários tipos ao mesmo tempo:** na tela **ALARMES → OPÇÕES** os botões
MAT / MEM / SEQ / REA ligam e desligam cada tipo (verde = marcado), e no
portal são caixas de seleção. Com mais de um marcado, **cada desafio sorteia
um tipo** entre os marcados — inclusive quando o alarme volta a tocar depois
de um erro — e nunca repete o tipo do desafio anterior. Pelo menos um tipo
fica sempre marcado. A tela do relógio mostra os marcados (ex.
`MAT+MEM - MEDIO`).

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
VIB:DUTY:0 … VIB:DUTY:100    intensidade (%) dos próximos ON / PULSE
STATUS?
ALARM:START | DISMISS | RESUME | STOP | SNOOZE | ESCALATED | TIMEOUT   (só log)
LIGHT:0 … LIGHT:100      RESERVADO: só com USE_MOSFET_LIGHT nas duas placas
```

Actuator → Display:

```
READY                        no boot
ACK:LIGHT:50 / ACK:VIB:ON    confirmação
STATUS:LIGHT:50,VIB:ON,DUTY:70
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
POST /api/alarms          list=HH:MM,ativo,dias;...  (até 6; dias = máscara
                          0-127, bit 0 = domingo; 0 = uma vez)
POST /api/save            ringMax (0/5/10/15/20/30), chalMask (bits 1=MAT
                          2=MEM 4=SEQ 8=REA), diff, chal, vib (0-3), vibW,
                          vibM, vibX (PWM %), volS, volX (volume %), ramp, light, wizIp, nightMode,
                          nightStart, nightEnd, tz
POST /api/vib/test        duty=0-100 → vibra 2 s (não salva)
POST /api/sound/test      lvl=1|4, pct=0-100 → toca 2 s (não salva)
POST /api/mode            demo=1 (demo) / demo=0 (producao)
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
- **O Wi-Fi liga direto no modo final** (`WIFI_AP_STA` sem rede salva,
  `WIFI_STA` com rede). No core 3.3.5 a lista de callbacks da biblioteca
  Network não tem trava: passar de `WIFI_STA` para `WIFI_AP_STA` nos ~2 ms
  depois de ligar a STA invalida o percurso da task `arduino_events`, e o
  watchdog reinicia a placa (`rst:0x8 TG1WDT_SYS_RESET`, sem Guru Meditation).
  Detalhes no topo de `net.cpp`.
- `WiFi.setHostname()` e `WiFi.setSleep()` vêm **antes** de `WiFi.mode()`:
  depois dele o core não os aplica.
- O touch é lido por **polling** a cada 20 ms, sem a IRQ do IO36: com o Wi-Fi
  ligado esse pino pode receber pulsos falsos (errata do ESP32).
- Layout e área clicável saem da **mesma tabela de botões** (`curBtns[]` em
  `ui.cpp`), então nunca saem de sincronia.
- Preferences corrompida (valores fora de faixa) volta sozinha para os
  padrões: alarme 07:00, nível Fácil, luz 100%.

---

## Pendências conhecidas

- **Testado na placa (v0.4):** boot sem reinício, portal, relógio, rampa e
  disparo do alarme no modo demo, toque e desafio com o Wi-Fi ligado, conexão
  na rede de casa e lâmpada WiZ. Ainda precisam de bancada o volume real do
  DAC e o comportamento dos MOSFETs.
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
- **Novo na v0.4, a conferir na bancada:** hora pelo NTP e
  `http://despertador.local` (no modo demo o NTP não é aplicado: passe para
  produção para testar); rampa da lâmpada com os tempos reais e a faixa de
  temperatura da sua lâmpada; som com o Wi-Fi ligado (o rádio também usa o
  Core 0, onde roda a task de áudio, e pode haver pequenos cortes); se segurar DESLIGAR por 2 s é confortável no touch
  resistivo (uma falha de leitura de até 150 ms não zera a contagem); se o
  vermelho e o brilho do modo noturno incomodam no quarto escuro
  (`C_NIGHT`, `BL_NIGHT`).
- O portal não tem login: qualquer pessoa na mesma rede consegue abrir.
  Aceitável para o protótipo; para produto, entra uma senha.
