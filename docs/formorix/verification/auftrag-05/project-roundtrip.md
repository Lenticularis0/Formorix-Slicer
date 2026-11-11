# Native 3MF-Projektportabilität

## Nachweisumfang und Prüfstand

Der abschließende Windows-Release-`ALL_BUILD` endete mit Exitcode 0.
Der anschließende [Portabilitätslauf einschließlich Namenskonflikt-Guard](package9-portability-final.txt)
bestand **19 Testfälle mit 1.149 Assertions**
(`[NativeRoundTrip],[FormorixProfiles],[FormorixSurfaces]`, Seed 42).
Der frühere [gezielte Paket-9-Lauf](package9-targeted-tests.txt) ist eine andere
Testauswahl und wird nicht zu dieser Abnahme addiert.

## Tatsächlich geprüfter Roundtrip

Der Test schreibt und liest das Projekt mit den nativen Orca-3MF-Funktionen.
Danach lädt er zuerst die eingebetteten Presets und anschließend die vollständige
Projektkonfiguration, entsprechend dem GUI-Import. Die
[Projektdatei](../../../../tests/data/formorix/auftrag05-three-materials.3mf) und ihre
[Beschreibung](../../../../tests/data/formorix/README.md) sind im Repository enthalten.

| Merkmal | Nachgewiesenes Ergebnis des Abschlusslaufs |
| --- | --- |
| Quellen | Neun native Presets: je drei Düsen-, Material- und Prozessquellen, einschließlich aktuell nicht ausgewählter Quellen |
| Kopfzuordnung | Düsendurchmesser 0,8 / 0,4 / 0,6 mm und Material-IDs **A / B / C** nach dem nativen Import erhalten |
| Modell | Ursprüngliche Vertices/Faces, drei Oberflächenmarkierungen, Bereichs- und Kopfzuordnungen unverändert |
| Prozesswerte | Absichtlich veraltete gespeicherte Snapshots verworfen; alle drei Profile frisch aus den importierten Orca-Quellen aufgelöst |
| Materialmenge | Regional 12 / 9 / 18 mm³, gemeinsam **39 mm³**; verbleibender Kern **321 mm³** |
| Materialprüfung | Fehlendes und überstehendes Material sowie Doppelbelegung und Kernüberschneidung jeweils 0 mm³ |
| Abstände | Regionale räumliche obere Schranke jeweils 7,105427357601005 × 10⁻¹⁵ mm; nominale Außenflächen-Schranke 0 mm |
| Erneutes Speichern | Alle neun Quellen erneut durch den nativen Sammler vorhanden; wiederholtes Ergänzen dupliziert keine Namen |
| Drei Bewegungsfolgen | Kopf-/Materialzuordnung und Zeitdaten vor/nach Import gleich; keine simultane Planung behauptet |

Die Werte beziehen sich auf das nominale Modell. Im importierten 3MF ist keine
analytische CAD-Referenz enthalten; `cad_verified` bleibt für alle drei Regionen
**false**. Es gibt dadurch keine zusätzliche 0,01-mm-CAD- oder physische Druckfreigabe.

## Kopien, Default und Konflikte

`append_project_sources` erzeugt vollständige Kopien auf der typgerechten nativen
Default-Basis. Die Originalpresets bleiben unverändert. Host-Zugangsdaten werden
aus den Kopien entfernt; der Test prüft ausdrücklich das Entfernen eines
synthetischen API-Schlüssels. Ein fehlendes Profil lässt die bisherige Ausgabeliste
unverändert. Implizite Default-Bindungen folgen der aktiven Projektkonfiguration;
ein explizit zugeordnetes, editiertes Default muss zuerst als benanntes Preset
gespeichert werden. Diese Default-Basis beschreibt keine kalibrierte Maschine.

Der Guard weist vor dem nativen Import gleichnamige, abweichende lokale Quellen
zurück. Die bestandenen Negativtests ändern die Temperatur der inaktiven
Materialquelle B von 205 auf 206 °C sowie den zweiten Modus von
`machine_max_junction_deviation` auf 0,02. Beide werden abgewiesen, ohne die
lokalen Werte oder die Projektannotation zu verändern. Saubere Quellen vor und
nach dem Import werden akzeptiert; nach Rücknahme der Modusänderung gilt das erneut.

Zwei anfängliche Fehlalarme wurden gezielt behoben: Native Importnormalisierung
fügt Leerzeichen in `thumbnails` ein und erweitert einen einzelnen
`machine_max_junction_deviation`-Wert auf zwei gleiche Moduswerte. Die reine
Vorschaubild-Spezifikation wird nicht verglichen; beim Modusvektor werden die
tatsächlich verwendeten Werte einschließlich des nativen Singleton-Fallbacks
verglichen. Eine echte Änderung des zweiten Wertes bleibt ein Konflikt.
Implizite Bindungen und alte externe Referenzen ohne eingebettete Quelle bieten
keinen Archivvergleich. Unbekannte zukünftige Formorix-Annotationen bleiben beim
Speichern opak erhalten; ihr Inhalt erhält damit keine neue Validierungsfreigabe.

## GUI und Grenzen

Die bisherige native GUI-Gegenprobe zeigte nach dem Laden drei zugeordnete Köpfe,
39 mm³ nominales Material und etwa 105 s für den geplanten 5D-Teil. Dies ist ein
Zwischenbefund; der finale GUI-Lauf einschließlich Konfliktbehandlung steht noch aus.
Die Quell-TCP-Offsets dieser Testprofile sind 0 mm; alte Snapshot-Offsets werden
nicht übernommen. Haftung, reale Ablage, Unterbaufestigkeit und Kollisionsfreiheit
sind nicht bestätigt. Die experimentellen Export-/Sendesperren bleiben bestehen.

Die übergreifende GUI- und Regressionsabnahme wird im
[Hauptbericht](../../../Formorix-Auftrag-05.md) geführt.
