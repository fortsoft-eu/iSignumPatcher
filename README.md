# iSignumPatcher

## Building with Visual Studio 2015

Open `iSignumPatcher.sln`, select **Release | Win32**, and press **Ctrl+Shift+B**.
The standalone executable is written to `bin\Release\iSignumPatcher.exe`.

The project uses the v140 toolset and the Win32 API, without the C runtime (CRT) or third-party DLLs. The required `postbuild.ps1` script runs automatically after the build and sets the PE minimum OS and subsystem versions to Windows NT 4.0. PowerShell is not required on the computer that runs the patcher.

## Usage

Place the patcher next to `iSignum.exe` and run it. Progress and results appear in the log window. Click **OK** when processing finishes.

A packed input is extracted in memory without running iSignum or accessing the Internet. After verifying the AlphaSkins pattern and the output file, the patcher replaces the original `iSignum.exe` with the unpacked application with custom styles disabled. It does not keep the original file or create a backup. If an error is detected, the original file is not replaced. Do not modify or run the input file while processing is in progress.

Known older releases without AlphaSkins are only unpacked when necessary. Already patched executables are left unchanged. When no file changes are needed, the log reports `INFO: Nothing to do.` Unknown application structures are rejected; future releases may require changes to the detector.

Patching invalidates the iSignum digital signature. The patcher does not process certificates.

The user has confirmed that the patcher works on **Windows NT 4.0, Windows 2000, and Windows XP**. It uses ANSI paths shorter than `MAX_PATH`. Only the resulting EXE is required to run it.

---

# iSignumPatcher

## Sestavení ve Visual Studio 2015

Otevřete `iSignumPatcher.sln`, vyberte **Release | Win32** a stiskněte **Ctrl+Shift+B**.
Výsledný samostatný soubor: `bin\Release\iSignumPatcher.exe`.

Projekt používá v140, Win32 API a žádné CRT ani externí DLL. Skript `postbuild.ps1` se automaticky spouští po sestavení a nastaví PE pro Windows NT 4.0; je nezbytnou součástí projektu. Na cílovém počítači není PowerShell potřeba.

## Použití

Umístěte patcher vedle souboru `iSignum.exe` a spusťte jej. Operace se vypíšou do okna; po dokončení stiskněte **OK**.

Packed vstup se rozbalí v paměti bez spuštění iSignum a bez Internetu. Po ověření vzoru AlphaSkins a výsledného zápisu patcher nahradí původní `iSignum.exe` rozbalenou aplikací bez stylů. Originál ani zálohu neponechává. Při detekované chybě jej nenahradí. Během operace soubor neupravujte ani nespouštějte.

Známé staré verze bez AlphaSkins pouze rozbalí, je-li to potřeba. Již patchovaný soubor znovu nemění. Neznámou strukturu odmítne; změna budoucí verze může vyžadovat úpravu detektoru.

Úprava zneplatní digitální podpis iSignum. Patcher se samotnými certifikáty nepracuje.

Uživatel ověřil funkčnost na **Windows NT 4.0, Windows 2000 a Windows XP**. Používají se ANSI cesty kratší než MAX_PATH. Pro provoz stačí jediné výsledné EXE.
