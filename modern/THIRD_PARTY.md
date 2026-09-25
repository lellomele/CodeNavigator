# Componenti

Il nuovo codice di Source Navigator è distribuito secondo GPL-3.0-or-later. La distribuzione storica conserva le proprie licenze e i propri avvisi.

| Componente | Versione dei sorgenti inclusi | Licenza / riferimento |
|---|---|---|
| Scintilla | 5.6.6 | Avviso permissivo in `third_party/scintilla/License.txt` |
| Lexilla | 5.5.3 | Avviso permissivo in `third_party/lexilla/License.txt`; consultare anche gli avvisi dei singoli lexer |
| Qt Widgets, PrintSupport, Core5Compat, traduzioni | 6.11.2 | LGPL/GPL secondo i moduli distribuiti; licenze incluse nel pacchetto |
| Motore Rust e dipendenze | `core/Cargo.lock` | Inventario `dependencies.json` e testi in `licenses/Rust` nel pacchetto |
| Source-Navigator NG | 4.5 | `../COPYING` e avvisi delle dipendenze originali |

Sorgenti ufficiali dell'editor: [Scintilla](https://www.scintilla.org/SciTEDownload.html), [Lexilla](https://www.scintilla.org/LexillaDownload.html).

SHA-256 degli archivi scaricati:

- `scintilla566.zip`: `a0c0cdf1cf226dc6252020ce9a87a939a8b615e65269db40ac9123b28fa20a9d`
- `lexilla553.zip`: `2092b1dd18355321717e3bde25148e4c87e691723ca2b06a65e29a307c5462a6`
- `mingw-w64-x86_64-qt6-5compat-6.11.2-1-any.pkg.tar.zst`: `1a710e4bfb7b9562ae33e0ac93f91c290376dc13f15cf338520d6fd0b9ca4490`, dal repository ufficiale MSYS2. Qt 6.11.2 e le traduzioni sono dipendenze di compilazione esterne al repository.

I sorgenti inclusi consentono di ricompilare i componenti. Qt è collegato dinamicamente; il pacchetto conserva i suoi avvisi. Prima della redistribuzione pubblica occorre completare il corredo di sorgenti e avvisi dei runtime effettivamente inclusi; la cartella `dist` è al momento una distribuzione locale di sviluppo.
