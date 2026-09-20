![Uploaded image](https://sdmntprpolandcentral.oaiusercontent.com/files/00000000-b7dc-820a-ab77-34d66abb40f2/raw?se=2026-09-20T06%3A07%3A57Z&sp=r&sv=2026-02-06&sr=b&scid=04440a8c-3e40-46a3-b53b-518cd4f60a2b&skoid=d4d17077-8f33-42a9-bd8b-aa9d5eaf5320&sktid=a48cca56-e6da-484e-a814-9c849652bcb3&skt=2026-09-20T05%3A47%3A03Z&ske=2026-09-21T05%3A47%3A03Z&sks=b&skv=2026-02-06&sig=/qRn0i%2BERCRSi8trxjJQ4DWGzmhSSQ0lpf9THEmSKhE%3D)

ich hab es schon zum Laufen bekommen auf einem Waveshare RP2350 PIzero mit einem SPI TFT und Touch! Yeah! Ich bin grade nicht im Lab. Wie genau wird der Kontext-Swich im Scheduler statt? Tief in event_multi()?

wird da der Stack umgeschaltet?

**Genau. Der Context-Switch in pTOS ist im Wesentlichen tatsächlich ein Stack-Switch plus Register-Restore.** Ich habe jetzt direkt `aes/arch/arm/gemasm.S` im aktuellen Repository angeschaut.

Beim ARM-Port passiert in `dsptch()` zunächst Folgendes:
    push {r0-r3, lr}...stmia r2, {r4-r11}...pop {r4-r7}stmia r2, {r4-r7}

Damit werden die Register gesichert. Wichtig ist der Kommentar: **die Rücksprungadresse `lr` bleibt auf dem Stack**. Sie wird also nicht separat als PC im Prozessblock gespeichert.

Dann kommen die entscheidenden Zeilen:
    cps #0x1fstr sp, [r1, UDA_SPUSER]cps #0x13str sp, [r1, UDA_SPSUPER]ldr sp, =gemasm_stack

Das heißt:
    laufender Prozess     │     ├── User-SP  ─────→ UDA_SPUSER     │     └── Supervisor-SP → UDA_SPSUPER                              │                              ↓                    SP = gemasm_stack                              │                              ↓                           disp()

**Ja: Der Scheduler verlässt also sogar den Stack des laufenden Prozesses und läuft anschließend auf einem eigenen Dispatcher-Stack `gemasm_stack`.**

Dann entscheidet `disp()` in C, welcher Prozess als Nächstes läuft, und macht:
    switchto(rlr->p_uda);

Und `switchto()` macht genau das Umgekehrte:
    cps #0x1fldr sp, [r0, UDA_SPUSER]cps #0x13ldr sp, [r0, UDA_SPSUPER]

Also:
                        target UDA                       │          ┌────────────┴────────────┐          ↓                         ↓     UDA_SPUSER                UDA_SPSUPER          │                         │          ↓                         ↓     User-Stack               Supervisor-Stack

Danach werden die Register zurückgeholt:
    add r0, r0, UDA_REGSldm r0, {r0-r11}

und dann kommt der eigentliche Knaller:
    ldr pc, [sp], #4

Das bedeutet: **PC wird aus dem Stack des neuen Prozesses geladen.**

Es gibt also gar kein klassisches
    context->pc = ...
    context->sp = ...

für den gesamten Zustand. Die Rücksprungadresse liegt einfach auf dem jeweiligen Prozess-Stack.

Der vollständige Mechanismus ist damit ungefähr:
    Prozess ASP_A:+----------------+| return address | ← LR, von dsptch()+----------------+| ...            |+----------------+        dsptch()           │           ├─ Register → UDA_A           ├─ USP      → UDA_A.SPUSER           ├─ SSP      → UDA_A.SPSUPER           │           └─ SP = gemasm_stack                    │                    ↓                  disp()                    │             nächsten Prozess                    │                    ↓              switchto(UDA_B)                    │           ┌────────┴────────┐           ↓                 ↓        USP_B              SSP_B                              │                       Register B laden                              │                              ↓                    PC ← *(SP_B++)                              │                              ↓                         Prozess B

Und `psetup()` zeigt noch deutlicher, wie clever das konstruiert ist. Beim erstmaligen Anlegen eines Prozesses wird dessen Startadresse einfach **auf dessen zukünftigen Stack gelegt**:
    ldr r2, [r0, UDA_SPSUPER]str r1, [r2, #-4]!str r2, [r0, UDA_SPSUPER]

Der Kommentar im Source sagt sogar ausdrücklich, dass dieses künstliche Stackframe in Vorbereitung auf das spätere `pop {pc}` erzeugt wird.

Damit behandelt `switchto()` einen **brandneuen Prozess und einen zuvor unterbrochenen Prozess fast identisch**:
    neuer Prozess:Stack enthält → Startadressealter Prozess:Stack enthält → Rückkehradresse hinter dsptch()

`switchto()` weiß gar nicht, welches von beidem vorliegt. Es lädt einfach den Stack und springt zu dem PC, der oben draufliegt.

### Aber für deinen RP2350 ist ein Detail entscheidend

Der GitHub-ARM-Code ist für das klassische **ARM-A/R-Exceptionmodell** geschrieben. Man sieht das deutlich an:
    mrs r3, cpsrcps #0x1fcps #0x13

Also System Mode/Supervisor Mode mit **banked Stack Pointers**.

Der **Cortex-M33 im RP2350 hat das nicht**. Dort hast du stattdessen:
    MSP  = Main Stack PointerPSP  = Process Stack PointerCONTROLException Stack Frame

Das historische Konzept passt aber hervorragend:
    pTOS ARM-A                  RP2350 / Cortex-M33--------------------------------------------------UDA_SPUSER         ≈        PSPUDA_SPSUPER        ≈        MSP / Kernelstackgemasm_stack       ≈        Scheduler/MSPgesicherte regs    ≈        Software contextPC auf Stack       ≈        Exception frame

Und jetzt verstehe ich auch viel besser, **warum deine RP2350-Portierung bereits GEM anzeigen kann**: Für den Cortex-M33 musst du genau diesen kleinen Maschinenkern von `gemasm.S` ersetzen. Der Rest des AES-Schedulers kann praktisch unverändert bleiben.

Der für mich interessanteste Punkt wäre deshalb jetzt dein RP2350-`gemasm.S`: **Wenn du dort momentan nur einen gemeinsamen Stack benutzt, könnte das zwar zunächst laufen, aber sobald zwei AES-Prozesse wirklich gleichzeitig alternieren, wird genau diese Stelle kritisch.**

Sources

Ja, genau. ich möchte mehrere Anwendungen laufen lassen können. Ist in pTOS gar nicht vorgesehen. Ich muss also etwas brainstormen.

Ja — und ich glaube, der entscheidende Gedanke ist: **Du musst nicht Multitasking komplett neu erfinden. pTOS hat überraschend viel davon schon eingebaut.** Was ihm fehlt, ist vor allem das Konzept **„mehrere normale GEM-Applikationen gleichzeitig“**.

Im aktuellen Code steht es sogar sehr deutlich:
    #define NUM_PDS (NUM_ACCS + 2)   /* acc's + ctrlpd + dos appl. */

Also: mehrere Accessories, `ctrlpd` und **genau ein Slot für die normale DOS/GEM-Applikation**. Gleichzeitig besitzt jeder `AESPD` bereits eigenen Zustand, Eventlisten, Queue und über `p_uda` sogar seinen eigenen CPU-/Stack-Kontext.

Das heißt für mich: **Wir sollten zunächst nicht den Scheduler umbauen. Wir sollten pTOS beibringen, normale Apps genauso als mehrere schedulbare AES-Prozesse zu behandeln wie heute die Accessories.**

Der vorhandene Dispatcher kann das prinzipiell schon:
                   rlr                │       ┌────────┼────────┐       ↓        ↓        ↓    APP A     APP B     APP C    AESPD     AESPD     AESPD      │         │         │     UDA       UDA       UDA      │         │         │    Stack A   Stack B   Stack C

`disp()` nimmt einen Prozess von `rlr`, verschiebt ihn je nach Zustand und ruft schließlich
    switchto(rlr->p_uda);

auf. Es ist dem Dispatcher dabei völlig egal, ob dieser `AESPD` ein Accessory, Desktop oder etwas anderes darstellt.

### Der eigentliche Haken liegt eine Ebene darunter: GEMDOS

Da wird es spannend. GEMDOS hat momentan:
    PD *run;      /* ptr to PD for current process */

also **einen globalen Zeiger auf den aktuell laufenden GEMDOS-Prozess**. Und `Pexec()` ist klassisch hierarchisch gebaut:
    Desktop   │   Pexec()   ↓Application   │   Pterm()   ↓Desktop

`proc_go()` setzt schlicht:
    run = p;
    gouser();

und beim Beenden macht `xterm()`:
    run = run->p_parent;...
    termuser();

Das ist genau das klassische Single-Tasking-TOS-Modell.

**Hier liegt meiner Meinung nach der eigentliche Umbau, nicht in `evnt_multi()` und nicht primär in `switchto()`.**

Ich würde deshalb einen gemeinsamen Task-Descriptor einführen, ungefähr:
    typedef struct {    AESPD aes;    UDA   uda;    CDA   cda;    PD   *gemdos_pd;    TASK_STATE state;} PTOS_TASK;

Dann hat eine Applikation wirklich:
                        PTOS_TASK                  ┌─────────────┐AES Scheduler  →  │ AESPD       │                  │   ↓         │Context switch →  │ UDA / PSP   │                  │             │GEMDOS          → │ PD          │                  └─────────────┘

Und genau hier kommt eine kleine, aber sehr wichtige Änderung in den Context-Switch:
    next = rlr;

    run = next->p_gemdos_pd;

    switchto(next->p_uda);

Natürlich nicht unbedingt exakt dort und mit diesem Feldnamen, aber **konzeptionell muss beim AES-Taskwechsel auch der GEMDOS-current-process wechseln.**

Sonst passiert nämlich etwas sehr Hässliches:
    APP A läuftrun = PD_AAPP A → evnt_multi()        ↓      Scheduler        ↓APP B läuftaber:run == PD_A       ← FALSCHAPP B → Fopen()

Dann würde GEMDOS den Dateizugriff mit dem Prozesszustand von A ausführen. Das betrifft nicht nur Dateien, sondern Speicherbesitz, DTA, aktuelle Verzeichnisse, Handles usw. Man sieht im GEMDOS-Code, wie intensiv `run` dafür verwendet wird; selbst die Dateihandle- und Directory-Zustände werden beim Erzeugen eines `PD` vom aktuellen `run` übernommen.

Mit
    AESPD A ↔ GEMDOS PD AAESPD B ↔ GEMDOS PD BAESPD C ↔ GEMDOS PD C

wird das plötzlich sauber.

* * *

Eine Sache würde ich dabei **bewusst noch nicht machen: Präemption**.

Deine Apps sollten zunächst weiterhin kooperativ laufen:
    App A   ↓evnt_multi()   ↓dsptch()   ↓App B   ↓evnt_multi()   ↓dsptch()   ↓App C

Das erspart uns eine riesige Menge Ärger. GEMDOS verwendet beispielsweise momentan sogar einen **gemeinsamen Supervisor-Stack für alle Prozesse**. Das ist explizit so im Source dokumentiert.

Solange kein Scheduler mitten in einem GEMDOS-Aufruf präemptiert, ist das beherrschbar.

Wenn wir hingegen einen Timer-IRQ sagen lassen:
    BÄNG → jetzt sofort App B

während App A irgendwo tief in `Fread()` steckt, müssten GEMDOS, VDI, AES, Filesystem usw. plötzlich alle reentrant sein. Das würde aus unserem kleinen Projekt ziemlich schnell ein Betriebssystemprojekt größeren Ausmaßes machen. 😄

Für **GEM-Applikationen ist kooperatives Multitasking sowieso sehr natürlich**, weil vernünftig geschriebene GEM-Programme die meiste Zeit in
    evnt_multi()

hängen.

* * *

Und jetzt sehe ich einen sehr schönen Weg für einen ersten Versuch.

Nicht sofort `Pexec()` kompatibel machen. Erst einmal bauen wir **zwei fest einkompilierte Test-Applikationen** beziehungsweise zwei native pTOS/RP2350-Programme:
    DesktopClock.appTest.app

mit beispielsweise vier Slots:
    #define MAX_APPS 4
    #define NUM_PDS (NUM_ACCS + 2 + MAX_APPS)

Das reicht noch nicht allein, weil `pd_index()` aktuell hart kodiert ist:
    return (i<2) ? &D.g_int[i].a_pd
                 : &D.g_acc[i-2].a_pd;

Auch hier sieht man das historische Modell sehr schön.

Das könnten wir zunächst in etwa auf
    0       ctrl1       desktop/system2..x    accessoriesx..y    applications

erweitern.

Dann starten wir zwei Apps jeweils mit eigenem
    AESPDUDAStackCDAEvent blocksGEMDOS PD

und lassen beide z. B.:
    for (;;) {    evnt_multi(MU_TIMER | MU_MESAG | ...);    redraw();}

laufen.

Wenn **zwei Fenster unabhängig aktualisieren**, während der Desktop weiterlebt, haben wir bereits den wichtigsten Meilenstein erreicht.

* * *

Danach kommt allerdings ein Punkt, der wesentlich schwieriger wird: **`Pterm()`**.

Aktuell beendet sich ein GEMDOS-Prozess nicht einfach und sagt dem Scheduler „ich bin fertig“. Stattdessen macht er:
    run = run->p_parent;...
    termuser();

und springt damit direkt zurück zum Elternprozess.

Für unser Modell brauchen wir eher:
    App B → Pterm()        ↓TASK_B = ZOMBIE/DEADAESPD B aus rlr/nrl entfernenGEMDOS PD B aufräumenSpeicher freigeben        ↓dsptch()        ↓irgendein anderer Task

**Das dürfte einer der zentralen Eingriffe werden.**

Ich würde deshalb `Pexec/Pterm` nicht als ersten Schritt anfassen, sondern zunächst einen pTOS-internen Mechanismus machen:
    ptos_spawn(...)
    ptos_exit(...)

Wenn das mit drei Apps stabil läuft, bauen wir danach die klassische GEMDOS-Semantik darauf.

* * *

Und noch etwas macht mir Hoffnung: **AES selbst ist bereits erstaunlich gut auf mehrere Clients vorbereitet.**

`fpdnm()` läuft bereits über alle AES-Prozessdeskriptoren, jeder besitzt `p_pid`, eine eigene 128-Byte Message Queue und eigene Eventblöcke.

Das heißt, Dinge wie
    appl_init()appl_write()evnt_mesag()evnt_multi()wind_update()

sind konzeptionell schon ziemlich nah an dem, was wir brauchen.

Und die Accessories beweisen sogar praktisch, dass **mehrere AES-Ausführungskontexte bereits parallel existieren können**: pTOS lädt jedes Accessory und erzeugt dafür mit `pstart()` einen eigenen Prozesskontext; beim Start wird sogar explizit mehrfach `dsptch()` aufgerufen, damit alle Accessories einmal laufen können.

Das ist für mich der entscheidende Befund:

> **pTOS hat bereits einen kooperativen Multitasking-Kernel im AES. Er wird momentan nur nicht als Multitasking-Betriebssystem für normale Anwendungen benutzt.**

Wir müssten also eher **die Grenze zwischen „Accessory“ und „Application“ aufbrechen**, statt ein neues Scheduling-System daneben zu setzen.

Und auf dem RP2350 würde ich das erstmal komplett auf **Core 0** machen. Core 1 bleibt für deine spätere Echtzeit-Steuerung frei. Damit hätten wir am Ende etwas ziemlich Hübsches:
    RP2350 CORE 0────────────────────────────────           pTOS AES scheduler ┌───────────┐ │ Desktop   │──┐ └───────────┘  │ ┌───────────┐  │ │ App 1     │──┼── cooperative └───────────┘  │    multitasking ┌───────────┐  │ │ App 2     │──┤ └───────────┘  │ ┌───────────┐  │ │ Accessory │──┘ └───────────┘RP2350 CORE 1──────────────────────────────── hard realtime Stepper / Servo / PID / PIO

Ich würde beim nächsten Schritt tatsächlich **die Datenstrukturen `THEGLO`, `AESPD`, `UDA` und GEMDOS `PD` nebeneinanderlegen und markieren, was global, was bereits pro Prozess und was noch zu per-process werden muss**. Dann sehen wir vermutlich ziemlich schnell, ob wir mit vielleicht 10–15 gezielten Änderungen zu einem ersten **pTOS mit Desktop + zwei gleichzeitig laufenden GEM-Apps** kommen.
