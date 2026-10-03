# Code Navigator

<img src="modern/assets/source-navigator.png" width="72" alt="Icona Code Navigator">

Applicazione desktop per navigare, cercare e modificare i sorgenti di un progetto. Qt 6, motore Rust/SQLite ed editor Scintilla/Lexilla. Italiano predefinito; inglese, francese e tedesco disponibili. Ispirata a Source-Navigator.

## Installazione

Scaricare **CodeNavigator-0.5.1-windows-x64.zip** dalle [release](https://github.com/lellomele/CodeNavigator/releases/latest), estrarre tutta la cartella e avviare **CodeNavigator.exe**. Windows x64; parser e librerie sono inclusi, senza installazioni separate.

## Uso

- **File → Apri progetto**: scegliere la radice e le estensioni da indicizzare. **Tipi di file** consente ordinamento ed esclusioni permanenti; **F5** aggiorna l'indice.
- Selezionare una cartella nel pannello **Progetto** limita tutte le ricerche a quella cartella e alle sottocartelle. Se si seleziona un file, si usa la sua cartella. **Intero progetto** ripristina la radice.
- Cercare contenuti, simboli o nomi dei file con testo, wildcard e regex; disponibili parole intere, distinzione maiuscole/minuscole e filtro percorso. Un clic mostra l'anteprima, un doppio clic apre l'editor. **Apri cartella** apre il gestore file.
- Espandere **Mostra opzioni di sostituzione**, spuntare i file e controllare l'anteprima prima di applicare. **Strumenti → Ripristina sostituzione** recupera gli originali.
- **Strumenti → Riferimenti incrociati** cerca chiamate, dichiarazioni, letture, scritture, ereditarietà e dipendenze. Disponibile anche dal menu contestuale dell'editor.
- **File → Stampa corrispondenze** stampa le corrispondenze nei file selezionati o esporta un PDF.
- **Preferenze** gestisce lingua, editor esterno, riapertura dell'ultimo progetto e sette temi personalizzabili, inclusi i chiari attenuati lino, salvia e nebbia.

## Limiti

File testuali fino a 32 MiB: UTF-8, UTF-16 con BOM e Windows-1252. Le ricerche mostrano fino a 5.000 risultati e segnalano limiti di tempo; anteprima e stampa riportano fino a 200 righe per file. La ricerca dei nomi include cartelle generate, esclude metadati e non segue collegamenti simbolici. I riferimenti incrociati sono sintattici: omonimi, macro, overload e chiamate dinamiche possono produrre risultati incompleti.

Le regex non supportano lookaround o backreference nel pattern. Nelle sostituzioni `$1` e `${nome}` richiamano i gruppi, `$$` inserisce un dollaro. Il filtro percorso usa wildcard e separatori `/`. Le sostituzioni su più file prevedono registro e copie di recupero, ma non una transazione unica. Le bozze, gli indici e le copie di recupero sono conservati nella cartella dati locale; i log si aprono da **Strumenti**.

## Compilazione

Servono Rust MSVC, GCC/MinGW, CMake, Ninja, Python 3 e Qt 6 per MinGW con Widgets, Test, Core5Compat e PrintSupport. La build corrente usa Qt 6.11.2.

```powershell
.\modern\build.ps1 -QtPrefix "C:/msys64/mingw64" -CompilerPrefix "C:/msys64/mingw64" -Package
```

Specificare il percorso del proprio SDK Qt; Rust, CMake, Ninja e Python devono essere nel PATH. La build esegue i test e prepara `modern/dist/CodeNavigator.exe` con tutte le dipendenze. I componenti dell'editor e i parser sono in `modern/third_party`. I sorgenti dei parser sono allegati alle release.

## Copyright e licenze

© 2026 Prof. ing. Raffaele Mele — [InfoTechLab](https://infotechlab.altervista.org/). GPL-3.0-or-later; vedere [LICENSE](LICENSE) e [Componenti](modern/THIRD_PARTY.md).
