# Unterbau und Stützstrukturen

Ein Bereich kann normalen Infill als Unterbau, die bestehende Einstellung **Infill first**, dichteren Unterbau oder zusätzliche Stützen anfordern. Die Hybrid-Vorschau prüft die gemeinsame Grenzfläche zwischen dem tatsächlich berechneten Kern und der 5D-Schale gegen die von Orca erzeugten Bahnen.

## Lokale Verdichtung

Mit **Dichterer Infill beziehungsweise Übergangsstruktur** und einer gewünschten Dichte kann die Vorschau eine lokale Verdichtung innerhalb vorhandener Kernschichten anfordern. Orca verarbeitet diese Bereiche mit seiner normalen Infill-Berechnung. Die übrigen Material- und Prozesswerte bleiben erhalten; insbesondere wird der Flow nicht angehoben. Eine ausgeschaltete Option oder eine Zieldichte, die die bereits verwendete Infill-Dichte nicht übersteigt, erzeugt keine Verdichtung.

Die lokale Verdichtung ist keine automatische Erzeugung neuer 5D-Stützstrukturen. Sie kann eine Lücke zwischen Infill-Bahnen verkleinern. Fehlt dagegen bereits in der konventionellen Schichtgeometrie eine geeignete Auflage, kann auch 100 % Infill diese fehlende Geometrie nicht ersetzen. Die Prüfung unterscheidet deshalb Bahnlücken, Lücken aufgrund der nativen Schichtlage und nicht ausreichend bekannte Materialgeometrie.

100 % Dichte kann Orca über dünnem Infill als interne Brücke berechnen. Deren hängender Querschnitt ist im Formorix-Kontaktmodell noch nicht verifiziert. Mehr Material bedeutet dann keine nachgewiesene Verbesserung der Auflage. Eine geringere, ausdrücklich gewählte Dichte wird separat mit ihren tatsächlich erzeugten Bahnen geprüft; Formorix ändert die gewünschte Dichte oder den Flow nicht automatisch.

## Aussage der Auflageprüfung

Die Proben liegen auf der gemeinsamen Kern-Schalen-Grenzfläche und besitzen Flächengewichte. Angezeigte fehlende Flächen sind aus diesen Proben geschätzte Werte; sie beweisen weder lückenlose Auflage noch Festigkeit. Eine geometrisch passende Auflage bestätigt auch keine Haftung zwischen unterschiedlichen Materialien.

Die rohe Orca-Bahngeometrie und die tatsächliche konfigurierte Materialmenge werden getrennt bewertet. Bei einem nicht kalibrierten Flow-Faktor, beispielsweise 0,98, kann die rohe Geometrie Kontakt zeigen, während die belastbare Beurteilung der abgelegten Materialform weiterhin **ungeprüft** bleibt. Eine höhere Infill-Dichte hebt diese Einschränkung nicht auf.

## Stützen und Bauabfolge

Die Orca-Support-Einstellungen im 5D-Tab sind dieselben Prozesswerte wie im normalen Einstellungsdialog. Änderungen wirken deshalb auch auf das herkömmliche planare Slicing. Support-Art, Muster, Abstände, Interface, Dichte über Musterabstand und Filamentzuordnung bleiben verfügbar.

Als Materialstrategie sind wegbrechbare Stützen, wasserlösliche Stützen wie PVA oder eine spätere automatische Auswahl vorgesehen. Bereits vorhandene native Orca-Support-Bahnen können in die geometrische Auflageprüfung eingehen. Zusätzliche 5D-Stützen, Materialverträglichkeit und eine automatische Druckreihenfolge werden nicht berechnet.

**Infill first** bleibt die vorhandene Orca-Prozesseinstellung. Erforderlicher Unterbau und darauf angewiesene 5D-Bereiche werden als Fertigungsabhängigkeit erfasst; das bedeutet nicht, dass ein Scheduler sie bereits ausgeführt oder zeitlich freigegeben hat.

Für die Herstellung einer vollständig in 5D gedruckten Wand muss vor dem 5D-Druck eine geeignete Tragstruktur vorhanden sein. Gegebenenfalls sind zunächst normale planare Stützstrukturen erforderlich.
