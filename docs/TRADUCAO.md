# Tradução dos textos do jogo

Os textos seguem o IDIOMA do menu. Em português e espanhol são traduzidos os textos em
japonês e os textos em inglês escritos em ASCII na ROM; em inglês, só os textos em japonês. O
inglês desenhado como imagem (rótulos de algumas telas, logotipos, "OPEN ALL BLOCKS!") fica
como está. A ROM não muda: a tradução é desenhada por cima (`runtime/translate.c`), então a
versão distribuível continua sem dados do jogo. O lockstep não traduz.

## Como funciona

**Japonês.** Todas as frases são imagens prontas (tiles carregados com a arte da tela),
mostradas como um grupo de sprites de 32 px de altura no pé da tela (Y = 176). No começo de cada
quadro o VDP calcula uma assinatura (FNV-1a dos bytes dos tiles) de cada sprite da parte de
baixo da tela (`scan_captions` em `vdp.c`). Os sprites cujas assinaturas estão na tabela
`captions` não são desenhados, e a frase traduzida vai no mesmo lugar, centralizada, em até
duas linhas equilibradas, com a fonte dos menus (só maiúsculas; acentos do português e do
espanhol) e contorno escuro.

**Inglês em ASCII.** O jogo escreve cada letra na tabela de tiles de um plano como código
ASCII + base da fonte (no terminal, `$694`). Os textos da tabela `texts` são procurados nos
planos A e B só nas cenas em que aparecem (`scan_texts` em `vdp.c`): qualquer base, mesma
paleta e prioridade em todas as letras, os espaços do começo incluídos, ao menos 4 letras ou o
texto inteiro. Como o terminal digita letra a letra, a tradução mostra a mesma fração. As
células achadas ficam transparentes e a tradução é escrita no lugar, na cor da fonte do jogo.
Os registros na ROM são `[posição na VRAM, 4 bytes] texto 00`, com tamanho fixo e endereços
usados pelo código, por isso a troca não é feita nos bytes.

**Para catalogar uma legenda nova:** `PM_DEBUG_CAPTIONS=1` lista os sprites candidatos
(posição, tamanho, assinatura) sempre que mudam. Cada legenda vai em `captions` com as
assinaturas dos seus sprites.

## Onde aparece

| Tela | Como chegar (testes) | Legendas |
| --- | --- | --- |
| História da abertura (cena $14, depois do terminal, sem apertar nada no título) | quadros ~2310-6500 | 7 |
| Noticiário depois da fase 1 (cena $24, mapa 9) | `PM_DEBUG_POKE=339,FD4A,9 PM_DEBUG_SCENE=340,24` | 1 |
| Noticiário do final (cena $30) | `PM_DEBUG_SCENE=340,30` | 4 |
| Seleção de fases (cena $20), uma por fase | `PM_DEBUG_POKE=520,F9AA,00XX`, XX = fases concluídas (bits): 00 mostra as fases 1-3, 07 as 4-6, 3F a final | 7 |

O noticiário entre fases só passa depois da fase 1 (mapa 8 → 9); depois das outras fases o
jogo volta direto à seleção (tabela de saída por mapa em $23180). Ainda falta conferir se há
texto em japonês dentro das fases (falas de chefe, avisos).

## Textos em japonês

"G・G" é a Galaxy Gang ("ギャラクシィ・ギャング"); nas traduções fica "G.G." (a fonte não tem
o ponto japonês). A garota da seleção de fases fala de forma informal ("〜の", "〜ね",
"〜よっ"); a jornalista, de forma formal.

### História da abertura

| # | Japonês | Português | English | Español |
| --- | --- | --- | --- | --- |
| I1 | 1999年…<br>数理生物学者　好山博士は | 1999... O biólogo matemático Dr. Yoshiyama | 1999... Mathematical biologist Dr. Yoshiyama | 1999... El biólogo matemático Dr. Yoshiyama |
| I2 | コンピュータ内に生み出した<br>人工生命体の少女と恋におちる | se apaixona por uma garota de vida artificial que ele criou dentro do computador. | falls in love with an artificial life-form girl he created inside the computer. | se enamora de una chica de vida artificial que creó dentro del ordenador. |
| I3 | メモリ空間内で結ばれた<br>ふたりの間に生まれたのが… | Da união dos dois no espaço da memória, nasceu... | Born to the two, united within memory space, was... | De la unión de ambos en el espacio de memoria, nació... |
| I4 | 現実界と　コンピュータ空間を<br>自由に行き来でき… | capaz de ir e vir livremente entre o mundo real e o espaço do computador... | able to travel freely between the real world and computer space... | capaz de ir y venir libremente entre el mundo real y el espacio informático... |
| I5 | 電気を操る少年<br>"パルスマン"である | o garoto que controla a eletricidade: "Pulseman"! | the boy who controls electricity: "Pulseman"! | el chico que controla la electricidad: "Pulseman". |
| I6 | そのころ…　世界各地で<br>謎の男　ドク・ワルヤマ率いる | Nessa época... pelo mundo todo, a gangue do misterioso Doc Waruyama, | Meanwhile... all over the world, the mysterious Doc Waruyama's gang, | Mientras tanto... en todo el mundo, la banda del misterioso Doc Waruyama, |
| I7 | "ギャラクシィ・ギャング"<br>による犯罪事件が多発していた | a "Galaxy Gang", cometia crime atrás de crime. | the "Galaxy Gang", was committing crime after crime. | la "Galaxy Gang", cometía un crimen tras otro. |

### Noticiário depois da fase 1

| # | Japonês | Português | English | Español |
| --- | --- | --- | --- | --- |
| N1 | パルスマンの活躍で　G・Gの<br>悪事は　ぶじ阻止されました!! | Graças ao Pulseman, o plano maligno da G.G. foi frustrado!! | Thanks to Pulseman, G.G.'s evil scheme has been foiled!! | ¡¡Gracias a Pulseman, el malvado plan de G.G. fue frustrado!! |

### Noticiário do final

| # | Japonês | Português | English | Español |
| --- | --- | --- | --- | --- |
| E1 | 世界的犯罪組織　G・Gの<br>本部が壊滅しました | A sede da organização criminosa internacional G.G. foi destruída. | The headquarters of the international crime syndicate G.G. has been destroyed. | La sede de la organización criminal internacional G.G. ha sido destruida. |
| E2 | しかし…ワルヤマと共に爆発に<br>まきこまれた　パルスマンは… | Porém... o Pulseman, que foi pego pela explosão junto com Waruyama... | However... Pulseman, who was caught in the explosion along with Waruyama... | Sin embargo... Pulseman, que quedó atrapado en la explosión junto con Waruyama... |
| E3 | 彼は　無事なのでしょうか… | Será que ele está bem...? | Is he safe...? | ¿Estará bien...? |
| E4 | パルスマンです!!<br>彼は　生きています!! | É o Pulseman!! Ele está vivo!! | It's Pulseman!! He's alive!! | ¡¡Es Pulseman!! ¡¡Está vivo!! |

### Seleção de fases

| # | Japonês | Português | English | Español |
| --- | --- | --- | --- | --- |
| S1 | G・Gが　TV局に侵入したの<br>電波で　人を操る作戦みたい!! | A G.G. invadiu a emissora de TV! Parece que querem controlar as pessoas pelas ondas!! | G.G. broke into the TV station! Looks like they want to control people over the airwaves!! | ¡G.G. entró en la cadena de TV! ¡¡Parece que quieren controlar a la gente con las ondas!! |
| S2 | 熱帯雨林が　荒らされてるっ!!<br>自然を　機械化するつもりね… | A floresta tropical está sendo devastada!! Eles querem mecanizar a natureza... | The rainforest is being ravaged!! They mean to mechanize nature... | ¡¡Están arrasando la selva tropical!! Quieren mecanizar la naturaleza... |
| S3 | カジノにドク・ワルヤマ出現!!<br>きっと　お金を盗む気なのよっ | O Doc Waruyama apareceu no cassino!! Com certeza ele quer roubar o dinheiro! | Doc Waruyama showed up at the casino!! I bet he's after the money! | ¡¡Doc Waruyama apareció en el casino!! ¡Seguro que quiere robar el dinero! |
| S4 | インド洋に　古代遺跡が浮上!!<br>また　G・Gの悪だくみなの? | Ruínas antigas emergiram no Oceano Índico!! É mais um plano da G.G.? | Ancient ruins have surfaced in the Indian Ocean!! Is it another G.G. plot? | ¡¡Unas ruinas antiguas emergieron en el océano Índico!! ¿Otro plan de G.G.? |
| S5 | アラスカに　秘密基地を発見!!<br>…あたし　イヤな予感がする… | Acharam uma base secreta no Alasca!! ...Estou com um mau pressentimento... | A secret base was found in Alaska!! ...I've got a bad feeling about this... | ¡¡Descubrieron una base secreta en Alaska!! ...Tengo un mal presentimiento... |
| S6 | G・Gが　潜水艦を造ってるわ<br>今度は海底…パルス大丈夫? | A G.G. está construindo um submarino. Agora é no fundo do mar... Pulse, você vai ficar bem? | G.G. is building a submarine. This time it's the seafloor... Will you be OK, Pulse? | G.G. está construyendo un submarino. Ahora es el fondo del mar... ¿Estarás bien, Pulse? |
| S7 | 聞いて　聞いてっパルスマン!!<br>G・Gの本部が　わかったの!! | Escuta, escuta, Pulseman!! Descobri onde fica a sede da G.G.!! | Listen, listen, Pulseman!! I found out where G.G.'s headquarters is!! | ¡¡Escucha, escucha, Pulseman!! ¡¡Descubrí dónde está la sede de G.G.!! |

## Textos em inglês (ASCII)

Ficam como estão: os nomes das músicas do teste de som (NEO TOKYO, SHUTDOWN...), o nome do
golpe (VOLTTECCER), os nomes nos créditos, o copyright, o aviso de região (não aparece no PC) e
a lista de fases do menu de depuração.

| Tela | Original | Português | Español |
| --- | --- | --- | --- |
| Título | START / OPTIONS | Jogar / Opções | Jugar / Opciones |
| Opções | OPTIONS, CONTROL, BGM, SE, VOICE, EXIT | Opções, Controle, Música, Efeitos, Voz, Sair | Opciones, Control, Música, Efectos, Voz, Salir |
| Opções | A/B/C ATTACK, A/B/C JUMP | Ataque, Pulo | Ataque, Salto |
| Terminal | >LOG ON: / >PASSWORD: | >Acesso: / >Senha: | >Acceso: / >Contraseña: |
| Terminal | >VERIFYING VOICE PATTERN: / >PATTERN MATCH | >Verificando padrão de voz: / >Padrão confere | >Verificando patrón de voz: / >Patrón correcto |
| Terminal | VOICE RECOGNIZED / PROCEED | Voz reconhecida / Prossiga | Voz reconocida / Adelante |
| Créditos | PULSEMAN STAFF / STAFF CREDIT | Equipe Pulseman / Créditos | Equipo Pulseman / Créditos |
| Créditos | DIRECTION & GAME DESIGN / PROGRAM / GRAPHICS | Direção e design do jogo / Programação / Gráficos | Dirección y diseño del juego / Programación / Gráficos |
| Créditos | MUSIC & SOUND EFFECTS / STORY PLOT / SPECIAL THANKS | Música e efeitos sonoros / Roteiro / Agradecimentos | Música y efectos de sonido / Guion / Agradecimientos |
| Fases | PUSH START BUTTON (fliperama da fase 7) / PERFECT BONUS! | Aperte Start / Bônus perfeito! | Pulsa Start / ¡Bonus perfecto! |
