# Formorix 5D-Oberflächen

Formorix ergänzt OrcaSlicer um projektbezogene Oberflächenmarkierungen und eine experimentelle, interne 5D-Bahnvorschau. Normale Orca-Projekte verwenden weiterhin den normalen Slicer. Experimentelle 5D-Druckaufträge bleiben für Slicing, G-Code-Ausgabe und Versand gesperrt.

1. [Oberflächen auswählen](selection.md), bei Bedarf angrenzende Bereiche zusammenfassen.
2. [Schichten und Schalendicke](layers.md), [Düsenausrichtung](orientation.md) und [Kopfzuordnung](multi-head.md) einstellen.
3. **5D-Vorschau berechnen** anklicken. Das Ergebnis erscheint im vorhandenen Tab **Vorschau**.
4. [Zeitschieber und Wiedergabe](preview.md) verwenden. Änderungen am Modell oder an relevanten Einstellungen erfordern eine neue Berechnung.

Weitere Themen: [Stützstrukturen](supports.md), [durchgehende Bahnen](continuous-paths.md), [Kopfübergänge](transitions.md), [High-Speed-Modus](high-speed.md).

Die Markierungen und Einstellungen werden in der 3MF-Projektdatei gespeichert. Vorschaugeometrie und Zeiten sind abgeleitete Daten und werden nach dem Laden erneut berechnet. Es entstehen keine ausführbaren 5D-Dateien.

[Demonstration mit Testprojekt und Screenshots](demo.md) · [Technischer Abnahmebericht](../Formorix-Auftrag-03.md)
