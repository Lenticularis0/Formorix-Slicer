# Auswahl von Oberflächen

Im Tab **5D-Oberflächen** ein Modell und einen Bereich auswählen. Ein Linksklick mit **Zusammenhängende Oberfläche** markiert angrenzende Dreiecke bis zur eingestellten Kantengrenze. Der Pinsel ergänzt oder entfernt Flächen; Umschalt-Klick beziehungsweise Rechtsklick entfernt Markierungen. Strg-Ziehen dreht die Ansicht, das Mausrad zoomt.

Die Neigungsauswahl misst den Winkel zwischen nach aussen gerichteter Oberflächennormale und globaler Z-Achse. Eine horizontale Oberseite hat 0°, eine senkrechte Wand 90°. Die Kantengrenze bezieht sich auf den Winkel zwischen benachbarten Dreiecken.

Mit Strg-Klick mehrere Einträge wählen. **Bereiche zusammenfassen** übernimmt die Einstellungen des zuletzt angeklickten Bereichs. Dessen Dreiecksmarkierungen werden zu einer gemeinsamen Maske. [Durchgehende Bahnen](continuous-paths.md) können anschliessend über die frühere Grenze laufen.

Die erste Bahnplanung verarbeitet einen ausgewählten Bereich auf einer Modellinstanz. Er muss zusammenhängend, ohne Löcher, in einer gemeinsamen Projektion eindeutig und in Bahnrichtung zeilenweise zusammenhängend sein. Mehrere getrennte Inseln, Überhänge innerhalb dieser Projektion und unbrauchbare Ränder werden abgelehnt. Die vollständige Ausgangsgeometrie muss ein geschlossenes, orientiertes Dreiecksnetz sein.

Ein Referenzkanten-Klick speichert zwei lokale Endpunkte. Für die Vorschau wird ihre Richtung mit der aktuellen Modell- und Instanztransformation ins Weltkoordinatensystem übertragen. Bei mehreren Teilen mit identischen Endpunkten ist die Referenz mehrdeutig; dann eine benutzerdefinierte Richtung verwenden. Eine Änderung der Netztopologie kann eine neue Markierung erfordern.
