# Source Navigator

<img src="modern/assets/source-navigator.png" width="72" alt="Icona Source Navigator">

Applicazione desktop per esplorare, cercare e modificare i sorgenti di un progetto a partire da una cartella radice. Interfaccia Qt 6, motore Rust/SQLite ed editor Scintilla/Lexilla. Italiano predefinito, inglese, francese e tedesco.

## Funzioni

- Indicizzazione incrementale con selezione delle estensioni e parser inclusi.
- Ricerca testuale e nei simboli: testo, wildcard, regex, parole intere, maiuscole/minuscole e filtro percorso.
- Anteprima rapida, sostituzione nei file selezionati, copie di recupero e controllo delle modifiche esterne.
- Editor con sintassi colorata, recupero delle bozze ed editor esterno configurabile.
- Riferimenti incrociati sintattici: chiamate, dichiarazioni, letture, scritture, ereditarietà e dipendenze.
- Stampa delle corrispondenze tramite stampanti Windows ed esportazione PDF diretta.
- Quattro temi personalizzabili giorno/notte, controllo dei contrasti e pannelli ridimensionabili.

## Installazione e requisiti

La piattaforma attualmente supportata è **Windows x64**. Questo repository distribuisce i sorgenti; non contiene un installer dell’applicazione. Dopo la compilazione, la cartella `modern/dist` contiene l’applicazione e le sue dipendenze: copiarla interamente e avviare `source-navigator.exe`. I parser non richiedono installazioni separate.

## Compilazione

Servono Rust con toolchain MSVC, GCC/MinGW, CMake, Ninja, Python 3 e un SDK Qt 6 per MinGW compatibile con il compilatore. Moduli Qt richiesti: Widgets, Test, Core5Compat, PrintSupport e traduzioni. La configurazione corrente usa Qt 6.11.2; Qt 5 non è supportato.

Dalla radice del repository, in PowerShell:

```powershell
.\modern\build.ps1 -QtPrefix "C:/msys64/mingw64" -CompilerPrefix "C:/msys64/mingw64" -Package
```

Specificare in `QtPrefix` la radice dell’SDK Qt effettivamente installato. Rust, CMake, Ninja e Python devono essere disponibili nel PATH. La compilazione esegue anche le verifiche automatiche. `modern/third_party` contiene i sorgenti dell’editor; `outputs` contiene la distribuzione storica con i parser utilizzati dalla build. I relativi sorgenti sono nelle directory originali del repository.

## Uso

1. **File → Apri progetto**: scegliere la cartella radice e le estensioni da includere. **Tipi di file** riapre l’ultima analisi; **Rianalizza cartelle** aggiorna l’elenco. **F5** aggiorna l’indice.
2. Cercare nel pannello centrale. Un clic mostra l’anteprima; un doppio clic apre il file nell’editor. Selezionare con le checkbox i file da sostituire e verificare **Anteprima sostituzione** prima di applicare.
3. **Strumenti → Ripristina sostituzione** recupera gli originali. Salvare o chiudere le schede modificate prima di sostituire o ripristinare.
4. **Strumenti → Riferimenti incrociati** (`Ctrl+Shift+R`) cerca le relazioni del simbolo. Il filtro parte dalla cartella selezionata nel progetto. Il clic destro su una parola nell’editor offre la stessa ricerca.
5. **File → Stampa corrispondenze** (`Ctrl+P`) stampa i file spuntati, oppure tutti quelli nei risultati se nessuno è spuntato. **Stampante…** apre la scelta stampante di Windows; **Salva PDF…** funziona anche senza stampanti installate. La stampa usa il contenuto salvato su disco.
6. In **Preferenze** si configurano lingua, colori, editor esterno e riapertura dell’ultimo progetto. Il cambio lingua si applica al riavvio. Le icone in alto a destra nascondono o espandono i pannelli; **Visualizza** li riapre.

Le wildcard testuali accettano `*` e `?`. Le regex supportano gruppi e alternative, ma non lookaround o backreference nel pattern. Nella sostituzione, `$1` e `${nome}` richiamano i gruppi; `$$` inserisce un dollaro. Il filtro percorso accetta wildcard sui percorsi relativi, usando `/` tra le cartelle.

## Limiti operativi

- File testuali fino a 32 MiB: UTF-8, UTF-16 con BOM e Windows-1252. La ricerca mostra al massimo 5.000 corrispondenze e segnala risultati parziali; l’anteprima e la stampa riportano fino a 200 righe per file, con estratti di 500 caratteri.
- I riferimenti incrociati sono sintattici: non risolvono semanticamente omonimi, overload, macro, chiamate dinamiche o dipendenze di sistema. Le regole coprono C/C++, Java, C#, JavaScript/TypeScript, Python, Rust e PHP, con limiti sulle sintassi avanzate. I parser storici forniscono ulteriori dichiarazioni. Aggiornare l’indice dopo modifiche esterne.
- Le sostituzioni su più file non costituiscono una singola transazione: il registro e le copie originali permettono il recupero di operazioni interrotte. I file modificati esternamente sono protetti dal ripristino automatico.
- Indici, preferenze, bozze e copie di recupero sono conservati nella cartella dati locale dell’applicazione. Le copie di recupero contengono i sorgenti; i log si aprono dal menu Strumenti.

## Copyright e licenze

© 2026 Prof. ing. Raffaele Mele — [InfoTechLab](https://infotechlab.altervista.org/).

Il codice in `modern` è distribuito sotto [GPL-3.0-or-later](modern/LICENSE). Source-Navigator NG 4.5 e le dipendenze originali mantengono copyright e licenze dei rispettivi autori; consultare [COPYING](COPYING) e gli avvisi nei sorgenti. Scintilla, Lexilla, Qt e le dipendenze Rust conservano le proprie licenze, indicate in [Componenti](modern/THIRD_PARTY.md).
