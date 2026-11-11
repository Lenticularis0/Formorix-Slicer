# Auftrag 04 – Nachtrag zu Materialablage und Übergängen

Der Nachtrag verbessert PR #5: konsistente nominale Materialmenge und Bahnabstände, endliche Randabschlüsse, symmetrische Grenzflächen benachbarter Schalen sowie eine differenzierte Prüfung der tatsächlichen Orca-Auflage. Die gemeinsame Hybrid-Vorschau und die experimentelle Drucksperre bleiben erhalten.

**Eine allgemeine geometrische Gesamtabweichung von höchstens 0,01 mm ist weiterhin nicht nachgewiesen.** Positive Nachweise gelten nur für ausdrücklich definierte analytische Referenzen im dokumentierten Nominalmodell. Insbesondere gekrümmte Flächen, unbekannte CAD-Referenzen und verbleibende Randlücken erhalten keine Freigabe. Reale Materialform, Haftung, Tragfähigkeit und Druckgenauigkeit sind nicht experimentell geprüft.

## Materialmenge und geometrisches Modell

Das bisherige starre Ellipsenmodell benötigte für kleine Welligkeit sehr enge Bahnabstände. Bei unverändert extrudiertem Querschnitt führte dies einschließlich der Randverbinder zum beobachteten Volumenfaktor **4,642359**. Eine bloße Abstandskorrektur beseitigt bei starren Ellipsen weder Leerraum noch überlagertes Material.

Das neue `CoalescedOrcaStadiumRibbon/v1` übernimmt Orcas nominale Zufuhr: `A = [h(w−h) + πh²/4] · f`. Eine angenommene kompaktierte Materialzelle hat normale Höhe `h` und Breite `p = A/h`. Profil-Linienbreite und Flow `f` bleiben unverändert. Die angenommene Verschmelzung zu solchen Zellen ist ausdrücklich unkalibriert. [Mathematische Definition, Mengenprüfung und Grenzen](formorix/verification/auftrag-04-nachtrag/deposition-model.md).

Endliche Gehrungszüge und flache Endkappen gehen in die Bilanz ein. Ungeeignete lokale Querschnitte beenden die Extrusion; Verbindungen ohne Materialauftrag werden als ungeprüfte Verfahrbewegungen dargestellt. Reststreifen werden nicht durch eine globale Änderung von Abstand oder Flow verdeckt. Oberflächenqualität verfeinert ausschließlich die Längsabtastung und Krümmungsprüfung. Im Qualitätstest steigt die Punktzahl von 213 auf 873 bei unverändertem Abstand und unverändertem Profil.

Die Mengenprüfung unterscheidet unzulässige Übermenge und starke Überversorgung. Mehr als 110 % des zugeordneten Schalenvolumens führt zur Ablehnung. Bereits kleinere positive Überschüsse oberhalb der numerischen Toleranz verhindern eine Genauigkeitsbestätigung. Der globale Boolean-Volumenrest wird nicht als regionale Mengentoleranz verwendet. Die Zeitleiste bilanziert die Materialmenge je tatsächlicher TCP-Weglänge und respektiert den Volumenfluss.

## Ergebnisse der Geometrieprüfung

| Referenz | Tatsächliches Ergebnis | Aussagegrenze |
| --- | --- | --- |
| Gespeicherte Wölbung, Düse 0,4 mm, Breite 0,42 mm, Höhe 0,2 mm, Flow 0,98 | Volumenfaktor **0,9973219898**; äußere Projektionslücke **0,2607927 %**; Projektionsüberstand **0,1032011 %** | Keine vollständige 0,01-mm-Bestätigung. Wegen fehlender Hüllenproben bleiben Maximal-, Mittel- und RMS-Gesamtwerte unendlich. |
| Kern-/Schalenteilung dieser Wölbung | Volumenrest **3,6379788 × 10⁻¹² mm³** | Volumenerhaltung ist kein Nachweis der Materialhüllfläche. |
| Kommensurable analytische Ebene | Maximal-, Mittel- und RMS-Stichprobenfehler jeweils **0 mm**; Volumenfaktor **0,9999999991**; vollständige nominelle Abdeckung aller Lagen | Positiver Fehlerbudgettest nur für diese definierte ebene Referenz und das unkalibrierte Modell. |
| Benachbarte analytische Ebenen, Düsen 0,8/0,4 mm | Identische Materialgrenze; Fehlerwerte jeweils **0 mm**; Volumenfaktoren **0,9999999991 / 1,0000000006** | Keine Haftungs- oder universelle Übergangsfreigabe. |
| Hydrofoil-Negativfläche | Volumenfaktor **0,8312086429**; Projektionslücke **16,2231432 %**; Überstand **0,0181264 %** | Berechenbare Diagnosevorschau, deutlich unvollständige Materialabdeckung. |
| Analytische NACA0012-CAD-Kurve | Max. **0,0003825424 mm**, Mittel **0,0000709480 mm**, RMS **0,0001020701 mm**, geprüfte obere Schranke **0,00099124 mm** | Import-/Diskretisierungsnachweis der Referenzkurve, kein vollständiger Hybrid-Drucknachweis. |

Weitere konvexe, konkave, geneigte und variable Testfälle samt Rohwerten stehen im [geometrischen Prüfbericht](formorix/verification/auftrag-04-nachtrag/geometry.md). Abdeckungsanteile gekrümmter Flächen sind Projektionsdiagnostik, keine vollständige räumliche Mengen- oder Hausdorff-Schranke. Die Prüfung bewertet sämtliche Lagen; eine korrekt bedeckte Außenlage kann fehlende innere Materialzellen nicht verdecken.

## Scharfe Grenzen und geneigte Düsen

Benachbarte Bereiche behalten eigene Normalen und Schalendicken. Die explizite Gehrungspolicy berechnet identische innere Randpunkte und übereinstimmende Dreiecksdiagonalen. CGAL prüft geschlossene Körper, disjunkte Innenräume und Volumenerhaltung. Es gibt keine Priorität nach Regionsreihenfolge oder Material. Die Originalkontur bleibt unverändert.

Die beiden gespeicherten Regionen des ursprünglichen `curved-wall.3mf` können nun geometrisch gemeinsam zerlegt werden: 20 gemeinsame Kanten, Volumenrest 0 mm³, maximale Ausgaberundungsgrenze 6,00115 × 10⁻¹⁰ mm. [Gehrungsprüfung](formorix/verification/auftrag-04-nachtrag/geometry-miter.md).

Das ist keine Druckfreigabe dieses Zweiregionenprojekts: Die gekippte starre Düsenquerschnittsform passt dort nicht in die verlangte normale Schichtdicke. Der Hybridplaner weist die Kombination ausdrücklich ab. Eine Erlaubnis von höchstens 15° Abweichung umgeht diesen Test nicht. Eine Gehrung, die vorhandene innere Dreiecke umklappt, wird ebenfalls zurückgewiesen; eine allgemeine lokale Neuvernetzung fehlt noch.

## Auflage auf tatsächlichen Orca-Bahnen

Die Prüfung verwendet die normalen Orca-Extrusionssegmente als endliche abgerundete Rechtecke statt als Ellipsen. Sie berücksichtigt deren Länge und Enden. Brücken, ungeeignete Querschnitte und nichtplanare konventionelle Segmente sind keine bestätigte Auflage. Die Suche erfasst nominale Kontakte bis 0,5 mm Abstand; die Akzeptanz bleibt bei höchstens `min(0,05 mm; Schichtdicke/4)`.

Im gespeicherten Wölbungsprojekt fehlen **6.006 von 22.400** geprüften Kontakten; vorher waren es 7.926. Alle 6.006 gehören zur Kategorie **Bahnabdeckung trotz vorhandenem Kern unvollständig**. Fehlender Kern, ausgeschlossene Füllung und unbekannte Geometrie zählen in diesem Fall jeweils 0. Größter gefundener Abstand: **0,494771 mm**. Für **54 Proben** existiert kein geeigneter Kontakt innerhalb des Suchbereichs. Diese Werte rechtfertigen keine Aussage zur Tragfähigkeit.

Die Vorschau zeigt die Ursachen getrennt: Rot für fehlenden Kern, Orange für ausgeschlossene Füllung, Magenta für fehlenden Bahnkontakt und Grau für unbekannte Geometrie. Die zusätzliche Prüfung sagt nichts über Materialhaftung oder vollständige Kontaktflächen zwischen den Stichproben aus.

## Build und automatisierte Prüfungen

Geprüfter C++-Quellstand: **`2d5db079508df40f68a20de83942221bb8a67524`**. Windows x64 Release, MSVC 19.51.36256 / Visual Studio 18. `ALL_BUILD` und CMake-Installation erfolgreich. Direkt startbar unter `build/Formorix-Auftrag04-Nachtrag/orca-slicer.exe`; [Buildzuordnung und SHA256](formorix/verification/auftrag-04-nachtrag/build-manifest.txt), [Buildprotokoll](formorix/verification/auftrag-04-nachtrag/build.txt).

| Prüfung | Tatsächliches Ergebnis |
| --- | --- |
| Vollständige verfügbare CTest-Regression | **3.162 bestanden, 6 Windows-Symlink-Skips, 0 Fehler**; 3.168 ausgewählt, 259,35 s. [Protokoll](formorix/verification/auftrag-04-nachtrag/regression.txt). |
| Geometrie, bisherige Vorschau und neues Materialmodell | **57 Fälle / 221.257 Assertions bestanden**. [Rohwerte](formorix/verification/auftrag-04-nachtrag/geometry-tests-final.txt). |
| Native Kernintegration und Exportschutz | **18 Fälle / 5.237 Assertions bestanden**. [Protokoll](formorix/verification/auftrag-04-nachtrag/core-tests.txt). |
| clang-tidy gegen `origin/main` | **Alle 28 betroffenen C++-Dateien bestanden**. Ein zuvor fehlender direkter `<optional>`-Include wurde korrigiert. [Protokoll](formorix/verification/auftrag-04-nachtrag/clang-tidy.txt). |
| Neue GUI-Texte | Sechs deutsche Übersetzungen ergänzt; vollständiger gettext-Lauf erfolgreich; Katalogprüfung 0 Fehler und 63 bestehende Warnungen. [gettext](formorix/verification/auftrag-04-nachtrag/gettext.txt), [Katalogprüfung](formorix/verification/auftrag-04-nachtrag/translations-check.txt). |
| Zentrale Hilfelinks | 12 GUI-Ziele, vier explizite Anker und 26 relative Linkvorkommen gültig; alle 21 eindeutigen HTTP-Ziele antworten 200 ohne Weiterleitung. Commit `0e7bba2d317ec671bcf75ad6944cee795ccab359` bleibt unabhängig vom Entwicklungsbranch. [Audit](formorix/verification/auftrag-04-nachtrag/help-links.json). |

Reproduzierbare Befehle:

```powershell
cmake --build build --config Release --target ALL_BUILD --parallel 2 -- /p:CL_MPCount=8 /nodeReuse:false
cmake --install build --config Release --prefix build/Formorix-Auftrag04-Nachtrag
ctest --test-dir build/tests -C Release -LE 'NotWorking|RequiresApp' --no-tests=error --output-on-failure --timeout 180 -j 2
scripts/run_clang_tidy.ps1 -Base origin/main -NoFetch -Jobs 2 -Yes
scripts/run_gettext.bat --full
```

Die vorhandenen Kennzeichnungen `NotWorking|RequiresApp` bleiben aus der automatisierten Gesamtauswahl ausgeschlossen. Native GUI-Abläufe werden gesondert geprüft. Die bisherigen Linkerhinweise `LNK4098`/LTCG bleiben bestehen. Der isolierte FFF-Lauf meldet weiterhin eine fehlende beziehungsweise leere optionale `info/nozzle_info.json` in seiner Testressourcenumgebung; die Assertions bestehen. Eingeschränkte MSBuild-Prozesskommunikation erforderte den normalen lokalen Build-Kontext. Betriebssystem-Schutzfunktionen wurden nicht verändert. Andere Betriebssysteme und reale Druckhardware sind nicht geprüft.

## Tatsächliche GUI-Prüfung

Der geprüfte Windows-Build wurde nativ bedient: Hybridberechnung der Wölbung, Geometriebericht, farbige Auflagepunkte, alle acht Sichtbarkeitsgruppen, Zeitschieber, ±1 s, Start/Pause bei 4×, Rücksprung zum Anfang, drei Düsenbeschriftungen und Hilfe-Link. Speichern und erneutes Laden der 3MF erhält Markierungen und Parameter; eine erneute Berechnung ergibt wieder 222,7 s für den 5D-Teil. Die gekippte Zweiregionengeometrie wird nachvollziehbar abgelehnt. Danach funktioniert normales Orca-Slicing mit 100 Schichten und sichtbaren Druckbahnen. Experimentelles Slicing und ausführbarer Export bleiben gesperrt. [GUI-Protokoll, Screenshots und Prüfprojekte](formorix/verification/auftrag-04-nachtrag/gui.md).

Ein einmalig leerer nativer Vorschaubereich trat nach einem fehlerhaften Start mit einer isolierten Testkonfiguration auf. Nach regulärem Neustart mit der vom Programm wiederhergestellten Konfiguration bestanden sowohl Hybrid → Normal als auch Hybrid-Abweisung → Normal. Die genaue Ursache dieses einmaligen Darstellungszustands ist nicht nachgewiesen; dafür wurde kein spekulativer Quellumbau vorgenommen.

## Verbleibende Grenzen

Noch nicht allgemein gelöst sind vollständige räumliche Materialhüllflächen auf gekrümmten Bereichen, lückenloser Randabschluss für beliebige Zellbreiten, stark gefaltete/umlaufende Flächen, beliebige scharfe Gehrungsnetze und geneigte reale Materialquerschnitte. Es wurden keine automatische Mehrkopfplanung, Klipper-Anbindung, Kollisionsfreigabe oder ausführbaren Hybrid-/5D-Dateien eingeführt. Auftrag 05 wurde nicht begonnen.
