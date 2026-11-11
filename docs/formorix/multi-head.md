# Mehrkopf-Betrieb

Das Projekt enthält drei unabhängige Köpfe mit Aktivierung, Orca-Materialprofil und Aufgaben. Ein Kopf kann normalen Aufbau, 5D-Oberflächen, Stützen und Verstärkung kombinieren oder automatische Aufgabenverteilung anfordern.

Jeder Kopf kann ein vorhandenes Orca-Prozessprofil und eine Düsenquelle verwenden. Die Auflösung berücksichtigt die eigene Düse, Linienbreite, Schichtdicke, Temperatur, Geschwindigkeit und Volumenflussgrenze. Änderungen an den gebundenen Profilen machen ältere Berechnungsergebnisse ungültig.

## Lokale Linienbreite

Optionale minimale und maximale Linienbreiten werden je Kopf in Millimetern deklariert. Sie erlauben der Randfüllung, die lokale Breite innerhalb dieses Intervalls anzupassen. Ohne diese Angaben bleibt die Profilbreite unverändert. Die Grenzen sind Prozessvorgaben und keine experimentelle Freigabe der Materialablage.

Die Materialzufuhr folgt dem tatsächlich verwendeten lokalen Querschnitt. Die Orca-Faktoren für Filament, Druck und Außenwand bleiben erhalten. Eine unfüllbare Randzone erhält keine zusätzliche Überextrusion. Eine kleinere Düse oder eine andere Bahnrichtung kann erforderlich sein; die räumliche Materialprüfung entscheidet weiterhin über fehlendes und überstehendes Volumen.

Die Betriebsstrategie ist automatisch parallel, materialabhängig oder [High-Speed](high-speed.md). Diese Rollen sind gespeichert; es gibt noch keinen Scheduler. Für die aktuelle Vorschau muss der ausgewählte Bereich genau einem aktivierten Kopf zugeordnet sein. Das kann Kopf 1, 2 oder 3 sein.

Die Zeitarchitektur enthält bereits drei getrennte Spuren. Jede Bewegung besitzt Kopf-ID, Anfangs-/Endzeit, Anfangs-/Endpose, Bewegungstyp, Extrusionsstatus, Bereich und Lage. Alle drei Spuren werden mit derselben Uhr ausgewertet. Die Vorschau kann damit später gleichzeitig unterschiedliche Kopfbewegungen zeigen.

Gegenwärtig hat nur der zugewiesene Kopf eine echte Bahn. Die anderen Spitzen erscheinen an schematischen Positionen neben dem Bereich mit Status **ohne Aufgabe**. Das sind keine Maschinen- oder Parkkoordinaten. Nach seiner letzten Bewegung wartet der geplante Kopf an seiner Endposition.
