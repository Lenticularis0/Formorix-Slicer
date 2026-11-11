# Hybrid-Vorschau

**Hybrid-Vorschau berechnen** im Tab **5D-Oberflächen** verarbeitet alle markierten Bereiche der gewählten Modellinstanz. Das Originalmodell bleibt unverändert. Formorix bildet geschlossene Schalen und entfernt deren Volumen aus einer Kopie des Körpers. Orcas bestehende Berechnung erzeugt die normalen Wände, Füllungen und gegebenenfalls Stützen dieses Kerns. Die Formorix-Bahnen erscheinen in derselben 3D-Ansicht.

## Bedienung

1. Ein geschlossenes Modell laden und zusammenhängende Oberflächen markieren. Für den Einstieg eine glatte Wölbung oder eine ebene Fläche verwenden.
2. Schichtanzahl beziehungsweise Schalendicke, Schichtdicke und einen aktivierten Kopf je Bereich festlegen. Senkrechte Flächen benötigen die ausdrückliche Freigabe und eine erreichbare nominale Düsenrichtung.
3. Unter **Mehrkopf-Betrieb** je Kopf das Orca-Material, das **Düsenprofil (Orca-Drucker)** und die **Düse im Profil** auswählen. Die leere Profilzuordnung folgt dem aktuell bearbeiteten Orca-Profil. Drei unabhängige Köpfe dürfen verschiedene Düsendurchmesser besitzen.
4. Mindestens einem Kopf die Aufgabe **Normaler Bauteilaufbau** zuweisen. Die Vorschau verwendet den ersten dafür aktivierten Kopf, ersatzweise den ersten Kopf mit automatischer Aufgabenverteilung. Für aktivierte Orca-Stützen gilt entsprechend die Aufgabe **Stützstrukturen**.
5. **Hybrid-Vorschau berechnen** wählen. Der **Geometriebericht** zeigt Aufteilung, Profile, Abdeckung, Materialvolumen, Auflage und getrennte Fehlerbeiträge.

| Anzeige | Inhalt |
| --- | --- |
| Originalmodell | Unveränderte Sollgeometrie des importierten Modells. |
| Verbleibender Kern | Tatsächlich berechneter Kern nach Abzug der Schalen. |
| Orca-Kernbahnen | Normale Extrusionsbahnen, nach Kopf unterschiedlich gefärbt. |
| 5D-Schale | Berechnete Schalenkörper. |
| 5D-Bahnen | Nominale Materialmittellinien; Gold bedeutet zukünftig, Grün bereits abgespielt. |
| Gemeinsame Grenzflächen | Grenzflächen zwischen Kern und Schalen. |
| Fehlende Auflage | Rot: kein konventioneller Kern; Orange: ausgeschlossene Füllung; Magenta: unvollständige Bahnabdeckung; Grau: unbekannte Geometrie. |
| Ungeprüfte Bereiche | Markierungen für unzureichend nachgewiesene Flächen. |

Die Zeitleiste umfasst **nur den 5D-Teil**. Kernbahnen bleiben als räumliche Referenz sichtbar und laufen nicht zeitlich synchron. Es gibt weder eine gemeinsame Fertigungsreihenfolge noch eine Kollisionsfreigabe. Gerade Verbindungen zwischen 5D-Lagen sind ungeprüfte Verfahrbewegungen.

Die Oberflächenqualität steuert die adaptive Auflösung entlang der Bahnen. **Fein** prüft kleinere Schritte und strengere Krümmungsabweichungen als **Standard** oder **Entwurf**. Der aus der Materialmenge abgeleitete Bahnabstand und der konfigurierte Flow bleiben dabei unverändert. Orange hervorgehobene Verfahrabschnitte sind keine Extrusion und nicht kollisionsgeprüft.

## Was die Prüfwerte bedeuten

Das Fehlerbudget trennt CAD-/Importfehler, Schalenberechnung, Float-/Orca-Konvertierung, Bahninterpolation und nominale Materialablage. **Unendlich** bedeutet, dass für diesen Beitrag keine belastbare obere Schranke vorliegt. Ein kleiner gemessener Maximalwert an Stichproben beweist keine Schranke für die gesamte Oberfläche. Ohne bekannte CAD-Referenz kann ein importiertes STL-/3MF-Modell keine allgemeine Bestätigung von 0,01 mm erhalten.

Die Materialvolumenbilanz vergleicht die geplante Extrusionsmenge mit dem berechneten Schalenvolumen. Das Modell **CoalescedOrcaStadiumRibbon/v1** übernimmt Orcas nominalen Querschnitt und den unveränderten konfigurierten Flow. Es beschreibt die Materialmenge als zusammengeflossenes Band mit gleichbleibender normaler Schichtdicke. Die daraus abgeleitete Bandbreite wird zusätzlich zur angeforderten Linienbreite angezeigt. Diese angenommene Materialform ist **nicht experimentell kalibriert**.

Abstände, Umkehrstellen und Bahnenden werden gemeinsam bilanziert. Unbedeckte Reststreifen und überlagerte oder außerhalb liegende Bandvolumen erscheinen im Geometriebericht. Eine plausible Gesamtmenge allein genügt nicht für 0,01 mm: Auch die vollständige Abdeckung aller Lagen und die lokalen Fehlergrenzen müssen nachgewiesen sein. Die projizierten Volumenwerte gekrümmter Flächen sind kein vollständiger räumlicher Nachweis. Flow wird nicht automatisch korrigiert.

Die Auflagenprüfung verwendet tatsächliche Orca-Extrusionssegmente und ihre nominalen abgerundeten Rechteckquerschnitte, einschließlich endlicher Bahnenden. Sie unterscheidet fehlenden Kern, bewusst ausgeschlossene Füllung, fehlenden Bahnkontakt trotz vorhandenem Kern und unbekannte Geometrie. Brücken und nichtplanare konventionelle Segmente werden nicht als bestätigte Auflage gewertet. Vorhandene Kontakte beweisen weder eine vollständige Tragfläche noch Materialhaftung oder mechanische Festigkeit. Verdichtete Übergangszonen und zusätzliche 5D-Stützen werden noch nicht automatisch erzeugt. **Infill first** und vorhandene Orca-Supportwerte bleiben native Orca-Einstellungen.

Ein vollständig in 5D gefertigter Körper kann einen leeren Kern ergeben. Dann entfällt der Orca-Kernpass; fehlender Unterbau bleibt sichtbar. Eine geneigte Düse verändert die normale Dicke des theoretischen Querschnitts. Nicht innerhalb der vorgesehenen Schale realisierbare Kombinationen werden abgewiesen; die Erlaubnis von 15° Abweichung allein ist keine geometrische Freigabe. Fehlende Materialhüllen-Prüfpunkte ergeben unendliche Fehlerwerte und werden nicht aus der Statistik entfernt.

## Grenzen

Der zusammenhängende Bahnplaner benötigt weiterhin einen lokal eindeutig projizierbaren Bereich ohne Löcher. Stark gefaltete Flächen und umlaufende Profile sind nicht allgemein unterstützt. An geeigneten scharfen gemeinsamen Kanten teilen symmetrische Gehrungsflächen die Schalen ohne Materialpriorität. Unlösbare Gehrungen, umklappende innere Randdreiecke und verbleibende Überlagerungen werden abgewiesen. Eine allgemeine Neuvernetzung solcher Kanten fehlt noch. Lokale Querschnittsprüfungen beenden ungeeignete Abschnitte; verbleibende Lücken und Überstände werden separat ausgewiesen. Diese Stichproben sind kein vollständiger räumlicher Einschlussnachweis. Nicht extrudierende Verbindungen erscheinen als ungeprüfte Verfahrbewegungen. Vollständige variable Wände benötigen weiterhin eine lokale Beendigung der Lagen.

Nichtneutrale XY-/Z-Schrumpfkompensation wird für Hybridberechnungen abgewiesen, da noch keine gemeinsame Kompensation von Kern und Haut besteht. Objekt-/Teilüberschreibungen, negative Volumen, Modifikatoren, normale Facettenbemalung und variable Orca-Schichthöhen werden ebenfalls ausdrücklich abgewiesen. So gehen ihre Wirkungen nicht unbemerkt verloren. Diese Einschränkungen gelten für den Hybrid-Prototypen; normale Orca-Projekte behalten ihre Funktionen.

Abgeleitete Profile werden bei Änderungen der zugeordneten Material-, Düsen-, Maschinen- oder Prozesswerte verworfen und vor jeder Berechnung neu aufgelöst. Markierungen und Zuordnungen werden im 3MF-Projekt gespeichert, Vorschauergebnisse werden neu berechnet.

Die Vorschau ist ein geometrischer Prototyp. Ausführbarer Hybrid-/5D-G-Code und Versand bleiben gesperrt. Normale Orca-Projekte verwenden unverändert den normalen Slicer.
