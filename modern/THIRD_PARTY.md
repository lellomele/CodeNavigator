# Componenti di Code Navigator

Code Navigator è distribuito sotto GPL-3.0-or-later. Gli avvisi originali delle dipendenze sono conservati.

| Componente | Versione | Licenza e sorgenti |
|---|---|---|
| Scintilla / Lexilla | 5.6.6 / 5.5.3 | Licenze permissive e sorgenti in `third_party` |
| Qt Widgets, PrintSupport, Core5Compat, traduzioni | 6.11.2 | Collegamento dinamico; licenze nel pacchetto, sorgenti in `RuntimeSources-0.5.1.zip` nella release |
| Rust / SQLite e dipendenze | `core/Cargo.lock` | Inventario `dependencies.json`, avvisi in `licenses/Rust`, sorgenti in `CodeNavigator-0.5.1-sources.zip` |
| Parser di linguaggio | Source-Navigator 4.5 | GPL-2.0-or-later; componenti in `third_party/parsers`, sorgenti `ParserSources-4.5.tar.gz` nella release |
| Runtime Tcl dei parser | 8.3 | Avviso in `third_party/parsers/TCL-LICENSE.txt`, sorgenti nello stesso archivio dei parser |
| Runtime MinGW e librerie ausiliarie | Inventario del pacchetto | Licenze in `licenses`; sorgenti LGPL e ricette MSYS2 in `RuntimeSources-0.5.1.zip` |

La distribuzione include solo i parser necessari al motore, senza l'applicazione Source-Navigator né il suo ambiente grafico.
