# Windows-GUI-Regression vor Merge von PR #4

Geprüfter Quellstand: 33cbcb2de841f7c4d3b7373a6cb378872a88d440, installierter Windows-Release-Build Formorix-Auftrag03B. Datum: 2026-11-11. Isoliertes Testdatenverzeichnis ../verification/gui-data.

Tatsächlich über die native Windows-GUI ausgeführt:
- curved-wall.3mf geöffnet; vorhandene Oberflächenbereiche und Schichtwerte geladen.
- 5D-Vorschau mit vier Schichten berechnet (213,8 s); Schieber auf 105,9 s bewegt, +1 s auf 106,9 s, -1 s zurück auf 105,9 s; Start und Pause bis 114,5 s geprüft.
- Schichtzahl auf fünf geändert, unter pr4-gui-roundtrip.3mf gespeichert, mit dem Windows-Dateidialog erneut geöffnet: Wert fünf blieb erhalten.
- Gemeinsamen Orca-Supportwert support_base_pattern_spacing im 5D-Tab von 2,5 auf 3 mm geändert und gespeichert. Archivwert 3 unabhängig geprüft.
- Kopf 1 Generic ABS @Prusa und Aufgabe 5D-Oberflächen zugewiesen; im nativen 3MF material_preset und tasks=2 nachgewiesen. Alle drei Kopfgruppen vorhanden.
- Hilfe am Kopf öffnet GitHub-Dokument multi-head.md am stabilen Commit 10177f248099015508246693bb67d8a5b0357101.
- Vorschau mit fünf Schichten berechnet (266,8 s), Rate 4x gewählt, gestartet, bei 48,5 s pausiert. Drei Düsenbeschriftungen, getrennte Kopfzustände, 5D-Dauereinschränkung und Hinweis auf ungeprüfte Verfahrbewegungen sichtbar.
- Zur Orca-Vorschau gewechselt. Zweites Testprojekt cube-planar.3mf mit Prusa MK4 regulär über die GUI geslict: 100 Schichten bis Z=20 mm, normale Wand-/Infill-/Solid-Infill-Bahnen, normale Exportfunktion aktiv. Formorix-Projekt behält die experimentelle Export-/Slicing-Sperre.
- Neigungsauswahl 85 bis 95 Grad markiert die senkrechte Wand sichtbar. Neuen Bereich angelegt und gewölbte Fläche direkt per Mausklick markiert (Farbwechsel). Drei Bereiche per Mehrfachauswahl zu einem Bereich vereinigt. Ergebnis als Testprojekt gespeichert.

Belege: pr4-gui-roundtrip.png, pr4-gui-preview.png, pr4-gui-orca.png, pr4-gui-vertical-selection.png, pr4-gui-merged-regions.png; gespeichertes Testprojekt pr4-gui-roundtrip.3mf. Die später zusammengefasste Testdatei enthält einen Bereich; die frühere Prüfung von fünf Schichten/Materialzuordnung wurde vor diesem Schritt ausgeführt.

Umgebungsgrenzen: Native Dialog-Fokusangaben waren teilweise widersprüchlich; über direkt beobachtete Dialog-Koordinaten und Zwischenablage funktionierten Öffnen/Speichern. Lokaler Browser-Connector deaktiviert; erfolgreiche Wiki-Navigation über den Chrome-Fenstertitel nachgewiesen. Ein Schließversuch des normalen Testfensters wurde durch automatische Freigabeprüfung abgelehnt; Fenster wurde offen gelassen. Keine Prüfung realer Druckhardware, Linux/macOS oder sämtlicher Orca-Dialoge. Dieser Durchlauf deckt die betroffenen Formorix-Abläufe und das normale Windows-Slicing ab.
