# Geometrische Prüfung des Hybrid-Prototyps

Prüfstand: 11. November 2026, Windows x64, Release. Der Zielwert von **höchstens 0,01 mm Gesamtabweichung zur CAD-Solloberfläche ist nicht nachgewiesen**. Gültige Kern-/Schalenvolumen und reale Vorschauwege werden berechnet. Die unverformte nominale Ellipse erzeugt bei dem für kleine Oberflächenfehler gewählten Bahnabstand jedoch erheblichen Materialüberschuss und an Bereichsrändern Überstand. Der Prototyp meldet diese Konflikte; er liefert keine Fertigungsfreigabe oder ausführbare Hybrid-Datei.

## Ausgeführte Prüfungen

`cmake --build build --config Release --target libslic3r_tests fff_print_tests -- /m:2` war erfolgreich. Nach dem unten beschriebenen GUI-Fund wurde `libslic3r_tests` erneut im Release-Modus gebaut. Der abschließende Lauf `libslic3r_tests.exe "[FormorixHybrid]"` bestand **13 Testfälle mit 2.194 Assertions**, einschließlich des repräsentativen 0,4-mm-Düsenprofils sowie sämtlicher Vollwand- und Ressourcenprüfungen. Das vollständige Ergebnis mit ungerundeten Messwerten steht in [geometry-tests.txt](geometry-tests.txt). Die unveränderte ursprüngliche Vorschau wurde zusätzlich mit `[FormorixPreview]` geprüft: **7 Testfälle mit 58.064 Assertions bestanden**.

Der tatsächliche GUI-Test deckte eine unnötige Längsüberabtastung auf: Der enge Querabstand für die nominale Hülle wurde im alten Raster-Seed zugleich als Längsschritt verwendet. Mit der standardmäßigen 0,4-mm-Düse und 0,42-mm-Linienbreite überschritt das mitgelieferte gewölbte Modell deshalb die Grenze von 100.000 Seedpunkten. Längsschritt und Querabstand sind jetzt getrennt: Nur der Hybrid-Seed verwendet höchstens 0,5 mm längs der Bahn; die Facettenkreuzungen werden weiterhin eingefügt und die kompensierte Kurve anschließend adaptiv verfeinert. Der Legacy-Standard bleibt unverändert. Die Grenzen von 100.000 Seedpunkten, 500.000 gesamten Bewegungen und zwei Millionen Layer-Meshelementen wurden nicht erhöht. Die geringere Assertionzahl im Vollwandtest folgt aus der geringeren Zahl geprüfter Längsknoten; keine der Prüfbedingungen wurde entfernt.

Die separate Integration in den tatsächlichen Orca-Print-Pfad wurde abschließend mit `[FormorixHybridCore],[Formorix5D]` geprüft: **13 Testfälle mit 5.203 Assertions** bestanden, darunter der berechnete Kern des mitgelieferten gewölbten 3MF-Projekts, das tatsächlich gespeicherte GUI-Profil ohne Exportmakros, native Wand-/Infill-Extrusionen, Schwindungsprüfungen und die gesperrten Exportwege. Der vollständige Build-/GUI- und Regressionstestbericht wird separat geführt.

## Messverfahren und Bedeutung der Zahlen

- Alle Schalenoperationen verwenden Double-Eingaben und CGAL mit exakten Prädikaten und Konstruktionen. Die Schalen werden mit dem ursprünglichen Festkörper geschnitten und vom verbleibenden Kern subtrahiert. Geprüft werden geschlossene orientierte Körper, Selbstüberschneidungen, exakte Volumenerhaltung, gegenseitige Überschneidungen und die Gültigkeit nach der gemeinsamen Ausgabe-Rundung. Angegeben ist das verbleibende **signierte Volumenresiduum nach Double-Materialisierung**, in mm³.
- `Float` ist die tatsächlich gemessene maximale Verschiebung eines Eingabepunkts beim Übergang in die Float-Meshdarstellung für den bestehenden Rasterplaner. Der zusätzliche Float-/Integerfehler des konventionellen Orca-Adapters wird erst bei dessen Integration addiert. Diese Zahl beweist keine Genauigkeit sämtlicher Orca-Algorithmen.
- `Max`, `Mittel` und `RMS` sind Abstände von Proben der markierten Meshoberfläche zu ausgewerteten Punkten der **nominalen äußeren Materialhülle**, in mm. Geprüft werden Dreiecksvertices, Kanten- und innere Gitterpunkte mit größenabhängiger Dichte. Die Mittelwerte sind probengewichtet; mehrfach vorkommende Kantenproben sind enthalten. Dies ist weder eine globale Hausdorff-Schranke noch eine Messung am gedruckten Teil. Bei gekrümmten Flächen ist auch die Bahnverfeinerung durch Mittelpunkte keine bewiesene Supremum-Schranke.
- Findet eine Probe keine auswertbare Hülle, zählt sie als `missing_samples`. **Max/Mittel/RMS werden dann unendlich**, nicht null und nicht nur über die verbleibenden Treffer gemittelt. Ein eigener Randabstands-Test prüft diesen Fall.
- `Lücke` ist der Flächenanteil der markierten Projektion, der nicht durch die nominellen Streifen innerhalb des Scallop-Ziels abgedeckt wird. `Überstand` ist die außerhalb der markierten Projektion liegende nominale Streifenfläche, geteilt durch die markierte Fläche. Überstand bedeutet nicht automatisch, dass das Gesamtbauteil verlassen wird: Material kann auch in ein angrenzendes konventionelles oder anderes 5D-Gebiet gelangen. Bei gekrümmten Flächen ist diese Projektion nur ein Diagnosemittel, kein vollständiger räumlicher Hüllennachweis.
- `Volumenfaktor` ist die Summe aus tatsächlichen Materialzentrum-Pfadlängen × nominaler Ellipsenfläche × eingestelltem Flow, geteilt durch das exakt aufgeteilte Schalenvolumen. Extrudierende Randverbinder sind enthalten; reine Verfahrbewegungen sind es nicht. Eine zweite Prüfung bilanziert die Vereinigungsfläche der überlappenden Ellipsen einer periodischen ebenen Bahnreihe. Der Flow wird nicht automatisch korrigiert.

## Erreichte Werte

Die kleinen Prüfmodelle besitzen einen 6 × 6 mm Grundriss. Sofern nicht ausdrücklich anders angegeben, wird ein innerer 3 × 3 mm Bereich mit zwei Lagen à 0,2 mm markiert. Kopf 1 besitzt 0,8 mm Düsendurchmesser und 0,88 mm Linienbreite/PETG. Die benachbarte zweite Region verwendet Kopf 2 mit 0,4 mm Düse, 0,44 mm Linienbreite/PVA und drei Lagen. Kopf 3 mit 0,6 mm Düse/PETG-CF wird zusätzlich bei der Extraktion und getrennten Profilzuordnung geprüft.

| Testgeometrie / Region | Max mm | Mittel mm | RMS mm | Residuum mm³ | Float mm | Lücke % | Überstand % | Volumenfaktor |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Ebene, analytische Referenz | 0,002616885 | 0,000941503 | 0,001365981 | 0 | 0 | 0 | 36,35556 | 3,670776 |
| Geneigte Ebene / variable Wandstärke | 0,002616885 | 0,000941503 | 0,001365981 | 0 | 0 | 0 | 35,94684 | 3,664356 |
| Konvexe Wölbung, innerer Bereich | 0,002215225 | 0,000802212 | 0,001078039 | +2,99349e-9 | 4,28862e-7 | 0,0143086 | 37,85862 | 4,171542 |
| Konkave Wölbung, innerer Bereich | 0,002550773 | 0,000690137 | 0,001096514 | −2,99349e-9 | 1,06629e-7 | 0,0143082 | 37,85862 | 4,100709 |
| Hydrofoil-Negativform, innerer Bereich | 0,003115463 | 0,000955464 | 0,001439233 | −4,41048e-10 | 2,10558e-7 | 0,00318676 | 36,40876 | 3,641356 |
| Benachbarte Flächen, PETG / Region 1 | 0,002616885 | 0,000904905 | 0,001345360 | 0 | 0 | 0 | 50,04444 | 3,885801 |
| Benachbarte Flächen, PVA / Region 2 | 0,002803116 | 0,000898058 | 0,001300036 | 0 | 0 | 0 | 25,37932 | 3,562999 |
| Senkrechte Wand, 75° Düse / 15° Abweichung | 0,019991610 | 0,003556305 | 0,005968074 | 0 | 0 | 0 | 36,35556 | 3,968186 |
| Vollständig markierte konvexe Oberseite, bis zum Bauteilrand | 0,002685818 | 0,000238936 | 0,000397593 | +2,99354e-9 | 4,28862e-7 | 0,000702757 | 23,06282 | 9,164551 |
| Mitgelieferte `curved-wall.3mf`, originale Wölbungsmarkierung allein | 0,001392431 | 0,000469346 | 0,000640251 | +3,63798e-12 | 0 | 0 | 3,620900 | 4,740572 |
| Dieselbe Wölbung, Standarddüse 0,4 mm / Linienbreite 0,42 mm / Flow 0,98 / 60 mm/s | 0,001389327 | 0,000497067 | 0,000692250 | +3,63798e-12 | 0 | 0 | 1,729853 | 4,642359 |

**Keiner dieser Fälle erhält eine globale 0,01-mm-Bestätigung.** Das ist ein überprüftes Ergebnis und kein fehlender grüner Teststatus. Die Tests prüfen, dass messbare Konflikte gemeldet und nicht als freigegebene Genauigkeit ausgegeben werden.

## Warum kleine Oberflächenfehler keine gültige Materialbilanz ergeben

Für eine unverformte Ellipse mit Breite `w`, Höhe `h` und Bahnabstand `p` gilt für eine große ebene Bahnreihe näherungsweise:

`zugeführtes Volumen / Sollvolumen = π·w / (4·p)`.

Das verwendete kleine Scallop-Ziel von 0,003 mm führt bei `w=0,88 mm`, `h=0,2 mm` zu einem Abstand von etwa 0,214 mm. Bereits die geraden Bahnen liefern dadurch ungefähr den Faktor 3,23. Die tatsächlich extrudierten Randverbinder erhöhen die gemessene Gesamtmenge weiter. Engerer Abstand verbessert bei diesem Modell die äußere Kontur, behebt aber nicht die Materialbilanz. Die gekrümmten Fälle verwenden bei stark wechselnden Normalen zusätzlich kleineren Abstand und können deshalb noch größere Faktoren aufweisen.

Der Planer verschiebt die Kontakt- und Materialzentrumkurven passend zum nominalen Querschnitt. Er verwendet jedoch noch keine vollständig an der Linienbreite orientierte Begrenzung der Materialhülle an Regionsrändern. Ein schlichtes Einrücken des Rasters würde andere Flächen unbedeckt lassen. Die vorhandenen Randüberstände werden deshalb numerisch ausgewiesen; sie werden nicht als gelöste Grenzflächenfertigung dargestellt. Ein kalibriertes bzw. physikalisch begründetes Ablagemodell und eine gemeinsame Flächen-/Volumenoptimierung bleiben erforderlich.

Bei der senkrechten Wand entsteht zusätzlich ein konkret nachgewiesener Kernkonflikt. Die um 15° zur Flächennormalen geneigte Ellipse hat bei `w=0,88 mm`, `h=0,2 mm` eine normale Materialdicke von

`2·sqrt((0,44·sin(15°))² + (0,1·cos(15°))²) ≈ 0,298656 mm`.

Die lokale Schale setzt weiterhin 0,2 mm Lagenabstand voraus. Die innere Materialhülle ragt daher etwa **0,098656 mm** über die gemeinsame lokale Kern-Grenzebene. Dieser Wert wird mit der Ellipsenstützfunktion an den Bahnproben gemessen, als Warnung ausgegeben und markiert. Die Materialbilanz bleibt ausdrücklich ungültig. Bei gekrümmten Grenzflächen ist diese lokale Normalprojektion eine Diagnose, keine vollständige Kollisionsberechnung.

## Geometrisch unterstützte und ausdrücklich begrenzte Fälle

| Fall | Tatsächlich geprüftes Verhalten |
|---|---|
| Ebene/geneigte, konvexe und konkave innere Bereiche | Gültige, sich ergänzende Volumenkörper und kontinuierliche nichtplanare Vorschauwege; Materialbilanz-/Abdeckungsstatus bleibt ungeprüft bzw. fehlerhaft wie oben angegeben. |
| Variable Wandstärke, konstante regionale Schalendicke | Die geneigte Oberseite wird gegen die tatsächlich lokal verfügbare Wandstärke geprüft. Zu große gewünschte Schalendicke wird abgewiesen. |
| Benachbarte Regionen mit unterschiedlicher Tiefe und Materialbreite | Getrennte Profile, gültige gemeinsame Volumenpartition und globale Layer-IDs. Die Darstellung ordnet die 5D-Teile zeitlich nacheinander an; sie behauptet keinen Mehrkopf-Scheduler. |
| Vollständig markierte konvexe Oberseite | Die seitlich bis zum Modellrand reichende Schale und ihre Materialzentren funktionieren. Der nominale Materialüberstand und Überschuss bleiben sichtbar. |
| Vollständig markierte geneigte, konkave und negative Hydrofoil-Oberseiten | Die kompensierten Materialzentren verlassen an den seitlichen Grenzen den Originalkörper. Explizite Fehlermeldung: lokale Bahnterminierung erforderlich. Kein akzeptiertes Teilergebnis bleibt zurück. |
| Originales `curved-wall.3mf` mit beiden Markierungen | Die Wölbung mit vier Lagen und die senkrechte Wand mit fünf Lagen erzeugen an ihrer scharfen Grenze überlappende Schalen. Die exakte Volumenprüfung weist die Kombination zurück. |
| Originale Wölbungsmarkierung dieses Projekts allein | Erfolgreiche Geometrie-/Bahnprüfung ohne Veränderung der originalen Flächendreiecke; zusätzlich tatsächliches konventionelles Orca-Slicing des berechneten Kerns geprüft. Für die GUI-Demonstration muss die zweite Region entfernt werden. Bloßes Ausblenden beeinflusst die Berechnung absichtlich nicht. |
| Vollwandmodus bei variabler/undefinierter Wandstärke | Explizite Ablehnung: lokale Schichtterminierung bzw. verlässliche Tiefe fehlt. Vollwandfertigung besitzt ohne vorherige Tragstruktur keine Tragfähigkeitsbestätigung. |
| Vollwandmodus eines gleichmäßig 1 mm dicken Quaders | Analytischer 4 × 4 × 1 mm Quader: genau 16 mm³ Haut, leerer konventioneller Kern, fünf Lagen und korrekte Materialzentrumhöhen. Ohne vorherige Kern-/Supportpfade werden sämtliche Unterbauproben als fehlend gemeldet. |
| Löcher, nicht zusammenhängende Flächen, überfaltete Projektion, vollständig umlaufende Profile | Der jetzige kontinuierliche Rasterplaner kann diese Fälle ablehnen. Das allgemeinere Atlas-Datenmodell allein ist noch keine universelle Bahnplanung. |
| Negative Volumen, Modifier, konventionelle Support-/Naht-/Material-/Fuzzy-Paintings | Hybrid-Berechnung wird ausdrücklich abgewiesen, weil diese Bedeutung noch nicht auf den neu berechneten Kern übertragen wird. Das originale normale Orca-Slicing bleibt verfügbar. |
| Objekt-/Teil-/Höhenbereich-Overrides und variable Schichthöhen | Ausdrückliche Ablehnung statt stillschweigender Verwendung des globalen Profils. Erlaubt bleiben Formorix-Konfiguration und die standardmäßige Orca-Extruderzuordnung 0/1; Formorix-Kopfzuordnungen bleiben separat. |
| Nichtneutrale Materialschwindung | Ausdrückliche Ablehnung im 5D-Modul und im Kern-Adapter, solange kein gemeinsamer kompensierter Körper für beide Verfahren existiert. |
| Große Fläche mit 200 Schichten | Der gesamte expandierte Vorschauumfang wird begrenzt: höchstens 500.000 Bewegungen und zwei Millionen kombinierte Layer-Meshvertices/-faces. Der Test überschreitet das Bewegungsbudget und erhält eine verständliche Fehlermeldung ohne akzeptierte Teilpreview. Die Grenze gilt über alle Regionen hinweg, nicht nur für den einschichtigen Raster-Seed. |

## Unterbau, Genauigkeitsbudget und verbleibende Nachweise

Der Unterbautest verwendet tatsächliche Orca-Extrusionssegmente mit deren Linienbreite und Schichthöhe. Geprüft werden Proben an der inneren Grenzfläche gegen nominale elliptische Querschnitte der konventionellen Ablage. Bei deaktivierter Verwendung von Infill zählt gewöhnliches Infill nicht als Auflage. Eine synthetische Prüfung unterscheidet vorhandenen und fehlenden Kontakt; die Integration prüft reale Kernpfade. Auch ein positiver Stichprobentest wird nie als vollständige Auflage-, Festigkeits-, Materialhaftungs- oder Fertigstellungsfreigabe behandelt. Dichte zusätzliche Übergangsstrukturen, zusätzliche 5D-Supports und die automatische Reihenfolge werden nicht erzeugt. Die Wünsche und die verbleibenden Prüfungen werden ausdrücklich angezeigt.

Die fünf getrennten Fehlerbudgets sind CAD-/Importabweichung, Schalenberechnung, Float-/Orca-Konvertierung, Bahninterpolation und nominale Materialablage. Bei der horizontalen und geneigten analytischen Ebene mit normaler Düsenausrichtung sind die ersten vier Komponenten endlich und im Test jeweils kleiner als 0,01 mm. Bei der senkrechten Wand mit Düsenabweichung bleibt die Interpolationsschranke unendlich. Die begrenzten Einzelkomponenten genügen wegen Materialüberstand und Materialüberschuss ausdrücklich nicht für die Gesamtbestätigung. Für die gekrümmten Mesh-Eingaben fehlt eine mitgelieferte, unabhängig zertifizierte CAD-Distanzreferenz; diese Komponente bleibt unbekannt. Das Hydrofoil ist eine negative, begrenzte NACA-0012-artige Prüfoberfläche, kein Nachweis für ein vollständig umlaufendes Flügelprofil.

Die gemeinsamen Interfaces werden aus denselben exakten CGAL-Dreiecken abgeleitet. Die ausgegebenen Festkörper und ihre Überschneidungsfreiheit werden nach der gemeinsamen Rundung geprüft. Ein zusätzlicher vollständiger Beweis, dass die separat dargestellte Interface-Triangulation auch nach jeder Entartungsbereinigung lückenlos mit beiden ausgegebenen Grenzflächen übereinstimmt, ist noch nicht implementiert. Es wird hierfür keine weitergehende Aussage gemacht.

Eine spätere Verbesserung muss insbesondere das Materialvolumen, die vollständige räumliche Oberflächenabdeckung, die Begrenzung der Hülle an Bereichsrändern, lokale Randterminierung, die normale Dicke geneigter Querschnitte und unabhängige CAD-/Interpolationsschranken gemeinsam behandeln. Diese offenen Punkte dürfen weder durch automatische unbelegte Flowfaktoren noch durch einen positiven Einzelkurven-Test verdeckt werden. Die reale Druckgenauigkeit bleibt unabhängig davon experimentell zu bestimmen.
