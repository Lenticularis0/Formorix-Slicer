# Mehrkopf-Betrieb

Das Projekt enthält drei unabhängige Köpfe mit Aktivierung, Orca-Materialprofil und Aufgaben. Ein Kopf kann normalen Aufbau, 5D-Oberflächen, Stützen und Verstärkung kombinieren oder automatische Aufgabenverteilung anfordern.

Die Betriebsstrategie ist automatisch parallel, materialabhängig oder [High-Speed](high-speed.md). Diese Rollen sind gespeichert; es gibt noch keinen Scheduler. Für die aktuelle Vorschau muss der ausgewählte Bereich genau einem aktivierten Kopf zugeordnet sein. Das kann Kopf 1, 2 oder 3 sein.

Die Zeitarchitektur enthält bereits drei getrennte Spuren. Jede Bewegung besitzt Kopf-ID, Anfangs-/Endzeit, Anfangs-/Endpose, Bewegungstyp, Extrusionsstatus, Bereich und Lage. Alle drei Spuren werden mit derselben Uhr ausgewertet. Die Vorschau kann damit später gleichzeitig unterschiedliche Kopfbewegungen zeigen.

Gegenwärtig hat nur der zugewiesene Kopf eine echte Bahn. Die anderen Spitzen erscheinen an schematischen Positionen neben dem Bereich mit Status **ohne Aufgabe**. Das sind keine Maschinen- oder Parkkoordinaten. Nach seiner letzten Bewegung wartet der geplante Kopf an seiner Endposition.
