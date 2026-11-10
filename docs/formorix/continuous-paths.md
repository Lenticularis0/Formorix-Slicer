# Oberflächenübergreifende Bahnen

Zusammengefasste Bereiche besitzen eine gemeinsame Dreiecksmaske und die Eigenschaft **Oberflächenübergreifende Bahnen: durchgehend**. Die erste Vorschau ignoriert frühere Bereichsgrenzen. Auf jeder berechenbaren Lage entsteht eine zusammenhängende, hin und her verlaufende Bahn. Dreiecksgrenzen fügen bei Bedarf Stützpunkte hinzu, aber keine Extrusionsunterbrechung.

Benachbarte Rasterzeilen werden entlang des ausgewählten Randes verbunden. Es gibt keine Retraktionen innerhalb einer Lage. Zwischen Lagen werden gesonderte Vorschau-Fahrbewegungen eingefügt. Diese direkten Verbindungen sind weder auf Freiraum noch auf Kollisionen geprüft.

Die Vorschau verarbeitet genau einen aktivierten Druckkopf und einen Bereich pro Berechnung. Ein zusammengefasster Bereich muss geometrisch zusammenhängend bleiben. Getrennte Inseln und Lochränder werden abgelehnt. Ein Linienraster muss die Projektion pro Zeile in genau einem Intervall schneiden; gegebenenfalls eine andere Bahnrichtung wählen.

Die Bahnrichtung ist automatisch, parallel zur ausgewählten Referenzkante oder ein Welt-Richtungsvektor. Sie wird in die mittlere Tangentialebene projiziert. Ein zur mittleren Normale paralleler Richtungsvektor ist ungeeignet. Abwechselnde Richtungen kehren die Reihenfolge der Bahn zwischen Lagen um. Der Randabstand wird in der Projektion gemessen und ist auf gewölbten Flächen nur eine Annäherung an den geodätischen Abstand.

Linienbreite und Geschwindigkeit stammen aus dem aktuellen Orca-Prozessprofil. Automatische Linienbreite verwendet die Düsenbreite als Ausgangspunkt. Die Geschwindigkeit wird zusätzlich durch den Volumenstrom des zugewiesenen Materials begrenzt. Ohne ausdrückliche Kopf-Materialzuordnung gilt das aktuell bearbeitete Orca-Materialprofil. Oberflächenqualität, Ironing, Temperatur, Kühlung und Flow-Strategien lösen noch keine zusätzliche Bahnoptimierung aus.
