# Pulseman PC — documentação técnica

Traduz o código 68000 da ROM japonesa do Pulseman (Mega Drive) para C e roda
nativamente, com uma camada que emula só o hardware: VDP (vídeo), controles e som
(Z80 rodando o driver de PCM do jogo, YM2612 via ymfm e PSG SN76489).

Nenhum dado do jogo fica no projeto: o C é gerado a partir da **sua** ROM no build
(`recomp/rom.sha1` confere se é a versão certa).

## Compilar

Requer Visual Studio 2019 (C++), Python 3.9+. CMake e SDL2 estão em `tools/`.

```sh
tools/cmake-3.30.5-windows-x86_64/bin/cmake -S . -B build -G "Visual Studio 16 2019" -A x64 -DPM_ROM=../Pulseman_Japan.md
tools/cmake-3.30.5-windows-x86_64/bin/cmake --build build --config Release
```

## Rodar

```sh
build/Release/pulseman.exe ../Pulseman_Japan.md
```

Controles: setas, Z/X/C = A/B/C, Enter = Start. Controles (joysticks) pela API GameController do
SDL (`runtime/gamepad.c`): direcional/analógico esquerdo, X/A/B (esquerda/baixo/direita) = A/B/C
como no controle de 3 botões, Y = A, Start = Start, Back/Guide = Esc; nos menus do PC os botões
viram teclas (direcional = setas, A/Start = Enter, B = Esc). Conectar e desconectar com o jogo
aberto funciona; `gamecontrollerdb.txt` ao lado do executável acrescenta mapeamentos. Depois de
fechar o menu, teclado e controle só voltam a valer soltos. Teste sem controle físico:
`PM_GAMEPAD_TEST=1` (com janela) conecta um controle virtual do SDL e aperta botões em tempos
fixos (a janela pode abrir sem foco, então o teste deixa o SDL ler controles fora de foco).

Ícone: `icon/pulseman.ico`, embutido no .exe por `runtime/pulseman.rc` (o SDL2 usa o mesmo
ícone na janela). Para trocar, ponha um PNG quadrado em `icon/icone.png` e rode
`py tools/make_icon.py icon/icone.png icon/pulseman.ico` antes de compilar.

### Versão distribuível (sem nada do jogo)

`pulseman_dist.exe` não tem nenhum arquivo gerado a partir da ROM: o código do jogo roda no
interpretador (Musashi, o mesmo que o lockstep usa como referência) direto da ROM do jogador.
Continuam as nossas partes: rotinas reescritas em C (`recomp/overrides.txt`), menus, saves,
filtros, abertura. Os ajustes de `recomp/patches.txt`, que na versão recompilada mudam o C
gerado, aqui são feitos por `runtime/dist.c`: as constantes (IMM) são gravadas na ROM em
memória a cada quadro e os ganchos (HOOK) rodam antes da instrução (gancho de instrução do
Musashi). As tabelas vêm de `recomp/gen_dist.py`, que lê só os .txt e o `rom.sha1`.

Compila sem a ROM (só esse alvo é criado quando `PM_ROM` não existe):

```sh
tools/cmake-3.30.5-windows-x86_64/bin/cmake -S . -B build_dist -G "Visual Studio 16 2019" -A x64 -DPM_ROM=nenhuma
tools/cmake-3.30.5-windows-x86_64/bin/cmake --build build_dist --config Release
```

O instalador leva essa versão (como `pulseman.exe`). Partindo do mesmo save, ela e a recompilada
ficam idênticas (RAM e tela) por 900 quadros de fase, nos dois modos. Partidas inteiras divergem
só pelo sorteio do jogo, que depende da contagem de ciclos (estimada na recompilada, exata no
interpretador). Custa ~5 ms por quadro (com "sem lentidão").

### Instalador para Windows

```sh
tools/cmake-3.30.5-windows-x86_64/bin/cmake --build build --config Release
powershell -ExecutionPolicy Bypass -File installer/build.ps1
```

Gera `build/installer/Pulseman-Setup.exe` (um arquivo, ~6 MB), feito em WPF (`installer/Setup.cs`
e `installer/ui.xaml`) com o compilador C# do .NET Framework 4.8, que já vem no Windows 10/11:
nada a instalar para compilar nem para rodar. Ele leva o jogo, o SDL2.dll, as imagens de `img/`
e o ícone; a ROM não vai junto: o jogador escolhe a dele (arrastar ou procurar; .md, .bin, .gen,
.smd ou .zip), conferida pelo SHA-1 de `recomp/rom.sha1` e copiada como `Pulseman_Japan.md`.
Instala por padrão em `%LOCALAPPDATA%\Programs\Pulseman` (sem administrador), cria os atalhos,
registra em "Aplicativos instalados" e copia `Desinstalar.exe` para a pasta. Textos em português,
inglês e espanhol (o idioma escolhido vira o do jogo numa instalação nova).

Papel de parede: `wallpaper.png` (ou .jpg/.jpeg) em `img/` ou `installer/`, 16:9; sem ele, fundo
animado. A arte fica livre à esquerda (alinhada à esquerda, com zoom lento), o título fica embaixo
sobre um degradê e o cartão de vidro escuro à direita; a cor de destaque é o magenta da arte. O
papel de parede não é instalado com o jogo e não entra nos logos da abertura.
Testes: `Pulseman-Setup.exe /shot PÁGINA arq.png [rom|-] [idioma]` (PageWelcome, PageRom,
PageOptions, PageProgress, PageDone, PageUninstall, PageError), `/selftest rom pasta` (instala sem
atalhos nem registro) e `/unselftest pasta` (remove; resultado em `%TEMP%\pulseman-unselftest.txt`).

O jogo é compilado com o runtime do C estático (sem Visual C++ Redistributable) e como aplicativo
de janela (sem console; rodado de um terminal, as mensagens vão para ele). Procura a ROM ao lado
do executável e avisa numa janela se ela faltar ou for outra.

### Menu de pausa (Esc)

Nas fases, o menu é integrado à pausa do próprio jogo (bit 5 de `$FFFB07`): apertar
Start pausa o jogo e abre o menu por cima; "Continuar" despausa o jogo apertando Start
no controle virtual. Esc durante a fase faz o mesmo. Fora das fases, Esc congela o
jogo e abre o menu direto.
Na tela de título, OPTIONS abre este mesmo menu (título "Opções"); o item "Opções do
jogo" leva à tela de opções original (controles e teste de som) e "Voltar" fica no título.
O menu tem páginas, no mesmo estilo da seleção de arquivos (fundo animado com o jogo
apagado por baixo, onda elétrica sob o título, barra de comandos no rodapé):
- Principal: Continuar, Salvar jogo, Carregar jogo, Configurações, Reiniciar e Sair, com
  um painel que explica o item escolhido e, numa fase, mostra vidas e pontos.
- Configurações: Modo de tela (widescreen/original), Sem lentidão, Volume (barra), Tamanho
  (janela 2x/3x/4x ou tela cheia), Filtro (ver abaixo), Moldura (desfocada/preta), Sprites (sem limite/original)
  e Idioma,
  com ← → para mudar e uma descrição de cada opção. Idioma: português (BR), inglês ou
  espanhol (só os menus do runtime; `idioma` no .ini, `--lang 0|1|2` na linha de comando).
- Salvar/Carregar: os mesmos cartões de arquivo da seleção de jogo.
Reiniciar, Sair, sobrescrever e carregar (com um jogo em andamento) pedem confirmação
SIM/NÃO. Teste: `--menu-shot QUADRO arq.bmp` com `PM_MENU_TEST=página,item,confirmação`
(página 0 principal, 1 configurações, 2 salvar, 3 carregar). Cada mudança é
gravada na hora em `pulseman.ini`, ao lado do executável (ou em
`%APPDATA%\Pulseman\pulseman-recomp` se essa pasta não aceitar gravação), e vale nas
próximas sessões. O caminho é aberto em UTF-8 (pastas com acento funcionam). Opções da
linha de comando (`--widescreen`/`--original`, `--overclock`, `--scale`, `--filter`,
`--sprite-limit`) têm prioridade; `--test-save` grava o .ini e sai.

### Logos extras na abertura

Imagens PNG ou BMP na pasta `img/` (ao lado do executável ou na pasta do projeto) aparecem
depois do logo da SEGA, em ordem alfabética, sobre a estática do próprio jogo. A cena da
SEGA ($00, rotina $2240) conta `$FFF804` até zero e então pede o título ($2340); o runtime
segura esse contador enquanto os logos passam (`runtime/intro_logos.c`), então a estática e
o chiado continuam. O plano A (o logo da SEGA) fica escondido até alguns quadros depois
de o título começar (no primeiro quadro ele ainda mostra a SEGA) e cada imagem "sintoniza" com
linhas tremidas, fica uns 2,5 s e some de volta na estática. Start/Enter vai para a
abertura (abaixo).

A imagem é preparada sozinha: o fundo (transparência, ou a cor do canto se for opaca) sai,
as margens são cortadas e ela é reduzida para caber em 220x110. O logo fica com as
cores da imagem, direto sobre a estática, como o da SEGA. Subpastas são ignoradas
(`img/desativados/` guarda logos fora da abertura).
Sem janela, só com `--intro-logos`; o lockstep não usa (compara com o jogo original). O PNG
é lido por `runtime/png.c` (sem dependências; não aceita PNG entrelaçado).

### Abertura em destaque

No original a abertura (cena $14: o terminal "LOG ON" e a história do Dr. Yoshiyama) só
aparece se ninguém apertar nada no título: o título conta `$FFF804` a cada quadro e, ao chegar
a zero, chama a abertura ($29DA). Qualquer botão novo a interrompe ($98C0: zera a rolagem
vertical em `$FFDEA2`, escurece a paleta com $1146 e pede o título), e no fim vem a
demonstração ($1C). Aqui (`runtime/opening.c`):

- Start na cena da SEGA, inclusive durante os logos extras, leva à abertura pelo caminho do
  próprio jogo: a SEGA termina (contador em 1, logos pulados), o título se monta (a estática e
  a paleta dele vêm desse caminho; pedir a cena $14 direto da SEGA deixava o título preto
  depois) e o contador do título vai a 1. Os quadros do título ficam pretos.
- Vinda dos logos, a abertura começa na história: o terminal (estados 0-6 de `$FFFACE`, tabela
  em $98FE) é pulado indo para o estado 7 ($9C5E, escurece em `$FFFAD0` quadros), e o estado 8
  ($9C8E) monta a história do zero; até lá a tela fica preta. A música da história ($22) é
  pedida no fim do terminal ($9B10), então ela é posta na fila de som (`$FFF700`, como $1D64).
  Pelo título parado, a abertura roda completa, como no original.
- Música da história: a da fase 2 (faixa $0B; a de cada mapa vem da tabela em $27BBA) no
  lugar da original ($22, que começa com uns 12 s de silêncio). Ela é pedida no fim do
  terminal ($9B10: `move.w #$22,d7`, IMM com `opening_music`) e, vinda dos logos, posta na
  fila de som junto com o pulo do terminal.
- Durante a abertura os botões não chegam ao jogo. Segurar Start, A, B ou C por 1 s faz o
  jogo receber um Start, mantido até ele aceitar (em alguns trechos a história passa quadros
  carregando sem ler o controle), e ele faz o próprio pulo. O aviso discreto ("segure para
  pular", cinza, com uma barra de 1 px que enche) só aparece quando um botão é apertado.
- Depois do Start nos logos e depois do pulo, os botões só valem de novo depois de soltos
  (senão o Start ainda seguro abria a seleção de jogo no título).
- No fim a abertura vai ao título, sem a demonstração: `hook_scene_change` ($83A, onde o jogo
  aceita o pedido de cena) troca o $1C por $04 e zera a rolagem vertical, como o pulo faz.

O lockstep não passa por aqui.

### Boss Rush

Terceiro item da tela de título (`runtime/bossrush.c`): os 7 chefes em sequência, com uma vida
só, a energia passando de um chefe ao outro, cronômetro e recorde.

- Título: START e OPTIONS são textos na fonte do jogo (registros em $2908, linhas 17 e 19,
  coluna 16, escritos por $1FE8) e o cursor é o caractere ':' na coluna 14 da linha do item
  (`$FFF807` = 0 ou 8; pisca pelo bit 4 de `$FFF805`). "BOSS RUSH" vai na linha 21 com a
  mesma fonte e os atributos do 'S' de START; com ele escolhido o jogo continua com o cursor
  em OPTIONS, e o cursor dele é apagado e desenhado na linha 21. Cima/baixo circulam pelos 3.
  O copyright (registro em $2C38, escrito por $2C1A) desce da linha 23 para a 25 (posição na
  VRAM trocada na ROM em memória, de $4B8E para $4C8E), para não ficar colado no item novo.
- Início: jogo novo pelo caminho normal (Start virtual com o cursor em START, sem a seleção de
  jogo). Assim que a seleção de fases ($20) se monta, faz o que a confirmação de fase faz
  ($38AE: mapa, bit 7 de `$FFFB66`, bit 0 de `$FFFD32`, cena $08) com a fase (`$FFF9AA`), o
  checkpoint (`$FFF9AC` + fase) e o mapa (`$FFFD4A`) da primeira arena. O mapa inicial de uma
  fase é 1 + tamanhos das anteriores (`$FFF9B4`: 10, 6, 8, 8, 16, 10, 13) + checkpoint ($3AD6).
  A passagem fica coberta por uma tela preta com o nome do modo.
- Arenas (fase/mapa): 1/7 (mundo virtual), 2/15, 3/23, 4/31, 5/47, 6/57, 7/70. Todas saem por
  $230AE quando o chefe foi derrotado (bit 0 de `$FFFAEE`), que avança o mapa e pede a próxima
  cena; no gancho de troca de cena ($83A) mapa, fase e checkpoint passam a ser os do próximo
  chefe e a cena vira $08. Depois do último, volta ao título com o resultado.
- Sem Stage Clear entre os chefes: ao vencer, o jogo pede o Stage Clear ($2C, em $21AC2), menos
  nos mapas $11, $41, $45 e $46, em que só liga o bit 0 de `$FFFAEE` e a arena sai sozinha
  ($21A8C). Na corrida a comparação com $46 vale o mapa da arena (IMM em $21AA0, função
  `bossrush_no_clear_map`). O trecho não era descoberto pelo recompilador (rodava no
  interpretador, onde o IMM não vale): $21A8C entrou em `recomp/extra_entries.txt`.
- Vidas extras (`$FFFD44`, BCD) ficam em zero: morrer leva ao continue ($18), que vira o fim da
  corrida (título com "game over", o chefe e o tempo). A energia (`$FFFD46`) segue a do jogo,
  que não a enche entre mapas.
- Cronômetro: quadros de fase (60 por segundo), parado na pausa do jogo, no alto da tela. O
  recorde fica no .ini (`boss_rush_recorde`, em quadros; os testes sem janela não gravam).
- Testes: `PM_DEBUG_BOSSRUSH=1` mostra cada troca de cena vista pelo modo; ligar o bit de
  chefe derrotado (`PM_DEBUG_POKE=quadro,FAEE,0100`) passa para a arena seguinte, mas só
  quando o jogo não o recalcula naquele quadro.

### Jogos salvos (3 slots)

"Salvar jogo" e "Carregar jogo" no menu abrem a lista dos 3 slots, com a data de cada um
e uma miniatura da tela do slot escolhido. Salvar num slot ocupado e carregar com um jogo
em andamento pedem confirmação (Enter de novo). No título só aparece "Carregar jogo".
Carregar fecha o menu e volta ao jogo no ponto salvo (se foi salvo na pausa, despausa).

START na tela de título (`$FFF807 = 0`, confirmado em `$2946`) abre antes a tela cheia
"Selecione o arquivo": três cartões (miniatura, data, vidas `$FFFD44` e pontos `$FFFD3C`,
lidos da RAM salva) e os botões "Novo jogo" e "Apagar arquivo". Setas ← → escolhem, ↑ ↓
trocam entre cartões e botões. Enter num arquivo carrega; num arquivo vazio, começa um
jogo novo (segue com um Start virtual). Apagar (botão ou Delete/Backspace no cartão) pede
confirmação SIM/NÃO. Esc volta ao título. Testes: `--files-shot arq.bmp` (sem janela, a
seleção abre no START e escolhe "Novo jogo"; `PM_FILES_TEST=arquivo,foco,apagar,confirmação`
escolhe o estado desenhado) e `--test-delete N` (apaga o slot N e sai).

Cada slot é um save state completo (`runtime/savestate.c`): 68000, RAM, VDP, Z80, YM2612
(formato do ymfm), PSG e controles, em `pulseman-slotN.sav` na mesma pasta do `pulseman.ini`.
Funciona porque o código recompilado só volta ao despachante entre blocos: entre dois
quadros nada fica na pilha do C. Um arquivo de outra versão do programa é recusado (e o
jogo atual é mantido). Testes: `--save-at QUADRO SLOT`, `--load-at QUADRO SLOT` (sem
janela, na pasta atual) e `--slots-shot QUADRO 0|1 arq.bmp` (submenu salvar/carregar).
Salvar no quadro 1800 e carregar em outro processo dá RAM, tela e som idênticos 500
quadros depois.

### Filtros

Menu FILTRO (ou `--filter N`):

| N | Filtro | Como funciona |
|---|--------|---------------|
| 0 | Nítido | escala da GPU, pixels quadrados |
| 1 | Suave | escala bilinear da GPU |
| 2 | Pixel suave | Scale2x duas vezes (4x) + bilinear: bordas arredondadas |
| 3 | Scanlines | linhas escuras entre as linhas do vídeo |
| 4 | TV | tubo: borrão do vídeo composto (mistura os pontilhados), feixe gaussiano, máscara de fósforo RGB, brilho difuso |
| 5 | TV curva | TV + tela curva (malha na GPU) e cantos escurecidos |

Scanlines e TV são calculados na CPU (`runtime/filters.c`) direto na resolução da área de
exibição, em luz linear e em várias threads (~2,5 ms por quadro em 1080p), então as
linhas ficam regulares em qualquer tamanho de janela. Testes: `--filter-shot QUADRO N
LARGURA ALTURA arq.bmp` (só a CPU, mede o tempo) e `--window-shot QUADRO arq.bmp` (a janela).

### 60 fps

- O jogo roda a 60 quadros/s exatos, um por atualização de um monitor de 60 Hz
  (o original roda a 59,92 Hz). O áudio é reamostrado para acompanhar.
- `--overclock N` (padrão 8): o 68000 roda N vezes mais rápido, o que elimina a
  lentidão do jogo original quando há muita coisa na tela. `--overclock 1` = original.
- `--renderer NOME` (padrão `direct3d11`): o D3D9 do SDL tem vsync instável em janela.
- `PM_DEBUG_LAG=1` conta quadros perdidos pelo jogo; `PM_DEBUG_PACING=1` mostra o ritmo
  real e cada travada da apresentação.

### Widescreen (padrão)

O jogo abre em 424x224 (~16:9): 52 px a mais de cenário de cada lado. O modo original
(320x224) fica no menu (Modo) ou em `--original`. O `pulseman_lockstep` usa o modo original
por padrão, já que compara com o jogo original.

- O renderizador desenha as colunas extras a partir dos planos do VDP.
- As rotinas de carregamento do mapa ($24EBE coluna, $24F94 linha) foram reescritas
  em C (`runtime/game/map_stream.c`) e carregam 4 metatiles a mais de cada lado.
  Sem widescreen elas são idênticas ao original (verificado pelo lockstep).
- A área central de 320 px fica idêntica ao jogo original, pixel a pixel.
- HUD: vidas/pontuação ($87C2/$87EC, `runtime/game/hud.c`) e o medidor de energia
  (objetos presos à tela dos tipos $08 e $3C, `runtime/game/objects.c`) são afastados
  para as bordas. Os outros objetos presos à tela (chefes e seus tiros) ficam no lugar,
  senão o desenho sai da posição da colisão. `PM_DEBUG_FIXED_OBJ=1` lista esses objetos.
- `PM_DEBUG_MAP=N` troca o primeiro mapa carregado (ex.: fase 3, EUA, começa no mapa 17,
  a arena do chefe); `PM_DEBUG_MAP=1` só mostra os mapas carregados.
- Objetos: o corte de sprites (X fora de -63..319, em $C4BC/$C4C0) foi estendido até as
  novas bordas.
- Ativação de objetos ($249FC): o jogo ativa a coluna da lista da fase que entra 32 px
  além da borda (câmera+22 / câmera-2 metatiles); no widescreen isso se afasta 4 metatiles,
  assim como a varredura do início da fase ($245CC) e as margens de desativação ($24C0C,
  $24C7E, $24CC8). `PM_DEBUG_SPAWN=1` mostra cada ativação (coluna, linha, tipo, bit) e avisa
  de objeto visível que ainda não foi ativado.
- Na varredura do início do mapa, o jogo só ativa quando o contador coincide com a câmera, e
  só a coluna exata da borda esquerda. Em mapas com a câmera travada ou dando a volta (salas
  de uma tela, arenas de chefe) a primeira borda do widescreen ficava 4 colunas antes da do
  original e os objetos dessas colunas sumiam: o chefe da fase 1 no laboratório (mapas 6 e
  8) não aparecia e o jogo travava depois da luta; 10 mapas tinham perdas. Os hooks
  `hook_sweep_*` ($245DE, $2463C, $24A6C, $24640, $24664) fazem esse primeiro encontro ativar
  também as colunas até a borda do original. Teste: `py tools/check_spawns.py` (todos os
  mapas: o widescreen tem de ativar tudo o que o `--original` ativa).
- Mudanças no mapa durante a fase ($18A06/$18B16/$18D3A, como o fio do chefe da fase 2): a
  célula muda no mapa da RAM e o plano A só é redesenhado se ela estiver na janela de
  $18C6E/$8DAE (de 16 px à esquerda da tela até 23 metatiles). No original o que fica fora
  dela está fora da tela e é recarregado antes de entrar; no widescreen um pedaço do fio
  recolhido nas colunas extras ficava no plano e, como essas colunas não são recarregadas,
  aparecia depois no meio da tela. A janela cresce map_stream_extra metatiles para os dois
  lados (IMM $18C82 e $8DD2, `hook_map_edit_column` em $8DE0 desfaz o deslocamento), cobrindo
  o anel de 32 metatiles do plano.
- Cutscenes montadas para 320 px ficam no formato original, com faixas pretas
  (`update_widescreen_for_scene` em `main.c`): a abertura da fase (cena $28, o símbolo do
  vilão e as setas que entram pelas bordas) e o noticiário com a jornalista ($30).
- Moldura (menu MOLDURA, `moldura_preta` no .ini): toda tela em 4:3 ganha laterais. São as
  cenas acima, as telas fora de fase cujas colunas extras saem de uma cor só (mapa-múndi,
  seleção de fases, opções, texto da abertura; `sides_empty` em `main.c`) e o modo original
  inteiro, que com a moldura desfocada também sai em 424 px, sempre com faixas e sem nenhum
  ajuste de widescreen (os 320 px do meio são idênticos aos do modo original em 320 px; com
  a moldura preta ele volta a 320 px). Por padrão as laterais mostram a própria cena ampliada
  1,5x, desfocada (blocos de 8 px, duas médias 3x3, bilinear) e escurecida, com sombra junto
  da imagem e suavizada entre quadros (`runtime/border.c`); só muda o que é mostrado.
  `PM_DEBUG_SCENE=1` mostra as trocas de cena; `PM_DEBUG_SCENE=quadro,cena` força uma cena
  (ex.: `340,30` logo depois do título).
- Pendências: nas bordas da fase a câmera não limita a área extra; ligar o widescreen no
  meio de uma fase pode pular os objetos das colunas extras até a câmera voltar.

### Sprites sem limite

O VDP mostra no máximo 20 sprites (320 px de sprite) por linha; o resto some ou pisca.
Com "Sprites: sem limite" (padrão) o renderizador desenha todos; a máscara de sprites
(sprite em X=0) continua valendo. `--sprite-limit` volta ao comportamento original e
`PM_DEBUG_SPRITES=1` lista as linhas que passariam do limite.

Rotinas reescritas em C ficam em `recomp/overrides.txt`; ajustes pontuais (constantes
e hooks) em `recomp/patches.txt`. Ambos são neutros no modo original (lockstep confere).

Opções de teste: `--headless --frames N --shot QUADRO arq.bmp --input roteiro.txt
--wav saida.wav --mute`. Com `PM_DEBUG_SOUND=1`, mostra estatísticas do som no final.

## Como funciona

- `recomp/m68k.py` — decodificador 68000 que emite C por instrução.
- `recomp/gen.py` — descobre o código (vetores, tabelas de salto/ponteiros,
  `recomp/extra_entries.txt`) e gera um bloco C por bloco básico em `build/gen/`.
- `runtime/` — despachante, barramento, VDP, E/S, som (`sound.c`, `ym_shim.cpp`), SDL.
- Código não descoberto cai no interpretador Musashi e o endereço vai para
  `missing_entries.txt`; `py tools/merge_missing.py build/run/missing_entries.txt`
  incorpora no próximo build.
- `pulseman_lockstep.exe` reexecuta cada bloco no Musashi e compara registradores,
  flags e acessos à memória — qualquer erro do tradutor aparece na hora.

## Terceiros (em `third_party/`)

- Musashi (68000, licença estilo MIT) — interpretador de fallback e referência do lockstep.
- superzazu/z80 (MIT) — CPU de som.
- ymfm (BSD-3) — YM2612.
- SDL2 (zlib) e CMake ficam em `tools/`.
