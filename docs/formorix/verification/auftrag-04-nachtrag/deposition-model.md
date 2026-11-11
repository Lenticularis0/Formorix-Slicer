# Nominales Materialmodell und unabhängige Prüfung

Das neue Hybridmodell heißt `CoalescedOrcaStadiumRibbon/v1`. Es beschreibt eine
ausdrücklich angenommene, kompaktierte Materialzelle. Es ist keine experimentell
gemessene Strangform und keine Aussage über reale Druckgenauigkeit, Haftung oder
Kollisionsfreiheit. Das bisherige starre Ellipsenmodell bleibt für die bestehenden
Grundlagenfunktionen unverändert verfügbar.

## Ursache des früheren Volumenverhältnisses um 4,64

Eine starre Ellipse mit Breite `w` und Höhe `h` hat den Querschnitt
`A = π w h / 4`. Für eine maximale Welligkeit `e` zwischen parallelen Kontaktbahnen
verwendet das bisherige Modell den Abstand

`p = w sqrt(2 e / b − (e / b)²)`, mit `b = h / 2`.

Bei Flow `1`, `h = 0,2 mm` und `e = 0,003 mm` ergibt sich bereits im unendlichen ebenen
Innenbereich `A / (p h) = 3,230696`. Kleinere Abstände bei veränderlichen Normalen
und zusätzlich extrudierte Randverbinder erhöhen diese Menge weiter; das erklärt
die Größenordnung des beobachteten Verhältnisses von rund 4,64.

Nur den Abstand auf `p = A/h = πw/4` zu ändern löst das geometrische Problem nicht:
Die Welligkeit beträgt dann `0,0381009 mm`. Die Vereinigung starrer Ellipsen füllt
nur etwa `88,4579 %` der rechteckigen Zielzelle; trotz passender Gesamtzufuhr bleiben
Leerraum und doppelt belegtes Materialvolumen. Eine globale Mengenbilanz allein
beweist daher weder vollständige Abdeckung noch eine maximale Abweichung.

## Zufuhr und kompaktierte Zelle

Die normale Orca-Querschnittsform ist ein Rechteck mit halbrunden Seiten. Für
`w >= h` berechnet das neue Modell dieselbe nominale Zufuhr in Double-Precision:

`A₀ = h (w − h) + π h² / 4`

`A_f = A₀ · f`

`p = A_f / h`

`f` ist der unveränderte konfigurierte Flow-Faktor. Eine kompaktierte rechteckige
Zelle hat damit die Breite `p`, die Normalhöhe `h` und exakt den Querschnitt `A_f`.
`p` ist eine abgeleitete Modellgröße; die im Orca-Profil angeforderte Linienbreite
wird nicht überschrieben. Vorhandene explizite Maßkorrekturen gehen in `w` und `h`
ein; der Hybridplaner verlangt derzeit eine neutrale Höhenkorrektur.

Eine mögliche Verschmelzung der einzelnen Stränge zu solchen Zellen ist eine
Modellannahme, die später mit Materialmessungen zu prüfen ist. Die Berechnung
behauptet nicht, dass ein frei abgelegter Strang von selbst rechteckig wird.

## Endliche Bahnen und Randbedingungen

Für ein ebenes Rechteck `W × D` mit `D = N p` lassen sich `N` durchgehende
Rasterzeilen exakt bilanzieren. Die Umkehrpunkte liegen jeweils eine halbe
Zellbreite vor dem Rand. Die erste und letzte flache Endkappe liegen auf dem Rand.
Bei korrekten Gehrungsverbindungen beträgt die gesamte Mittellinienlänge inklusive
der Verbinder `L = N W`. Somit gilt `A_f L = W D h`.

Die endliche Kontur wird als zusammenhängender Gehrungs-Sweep geprüft. Eine bloße
Vereinigung unabhängiger Segmentrechtecke wäre an den Ecken falsch. Reiseabschnitte
tragen kein Material bei. Für die Zeitleiste wird die Materialmenge eines Segments
auf dessen tatsächliche TCP-Länge umgerechnet; bei gekrümmten Bahnen oder einem
Kontaktpunktversatz unterscheiden sich Materialmittellinie und TCP-Länge.

Passt `D` nicht zur festen Zellbreite, darf der verbleibende Streifen nicht durch
eine heimliche Flow-Änderung oder eine bloße globale Abstandsnormalisierung
verschwinden. Fehlende Abdeckung, örtliche Randabbrüche und nicht ausreichend
geprüfte gekrümmte Bereiche bleiben ausdrücklich sichtbar. Eine starre geneigte
Düsenquerschnittsform kann die vorgesehene Normalhöhe überschreiten; solche
Konflikte werden vorerst abgewiesen, statt eine ungeprüfte Scherung einzuführen.

## Bedingungen einer positiven nominalen Genauigkeitsprüfung

Die kommensurable analytische Ebene besitzt einen positiven Test für das definierte
Nominalmodell. Diese Aussage setzt bekannte CAD-Referenz, überprüfte Konvertierung,
endliche Flächenabdeckung **jeder** Lage und eine entsprechende Materialbilanz
voraus. Sie ist keine pauschale Freigabe anderer Geometrien.

Relative Flächen- oder Volumenanteile begrenzen keinen maximalen lokalen Abstand.
Die Prüfung verwendet deshalb getrennte Abdeckungs- und Mengenbedingungen;
eine vollständig abgedeckte äußere Lage darf fehlende innere Zellen nicht verdecken.
Die Mengenkontrolle verändert keine Zufuhr: Jede positive Übermenge oberhalb des
deklarierten numerischen Restes gilt als unzulässig. Mehr als 110 % des Zielvolumens
zuzüglich dieses Restes wird gesondert als starke Überversorgung erkannt.

Der Ledger verwendet `|numerischer Rest| + 10⁻⁸ max(1, Zielvolumen)` in mm³.
Die Hybridprüfung übergibt konservativ einen numerischen Rest von `0` und verwendet
damit `10⁻⁸ max(1, regionales Schalenvolumen)` in mm³. Ein globaler Rest der
Kern-/Schalenpartition wird nicht als Toleranz für eine einzelne Region verwendet.
Unterfüllung erzeugt keinen positiven Übermengenstatus; sie muss weiterhin durch
Abdeckungsprüfung und Volumenbericht erkannt werden. Dieser Ledger ist selbst
kein Hausdorff-Abstandsbeweis.

## Ausgeführte und noch offene Prüfungen

Der abschließende [gezielte Testlauf](geometry-tests-final.txt) bestand mit
**57 Testfällen und 221.257 Assertions**.
Der zugehörige FFF-Testlauf bestand mit **18 Testfällen und 5.237 Assertions**.
Der gezielte Lauf enthält die neuen `[RibbonModel]`-Grundlagentests: drei unterschiedliche Düsen,
mehrere unveränderte Flow-Werte, Vergleich gegen Orcas `Flow`, Volumenflussbegrenzung,
ungültige Querschnitte sowie endliche Rechtecke mit 1, 2 und 5 Rasterzeilen.

Die ebenfalls bestandenen `RibbonBalance`-Tests prüfen die frühere Größenordnung
4,642359, kleine bereits unzulässige Übermengen, die 110-%-Grenze, numerische Reste,
Unterfüllung sowie ungültige und nichtendliche Eingaben. Die abgeschlossene
Gesamtregression und GUI-Abnahme sind im [Abschlussbericht](../../../Formorix-Auftrag-04-Nachtrag.md)
und im [GUI-Protokoll](gui.md) dokumentiert.
