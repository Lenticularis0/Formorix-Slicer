# Nominales Materialvolumen

Die Hybrid-Vorschau berechnet dreidimensionale Körper für das theoretisch abgelegte 5D-Material. Sie verwendet die tatsächlich geplanten Bahnabschnitte mit ihren lokalen Linienbreiten, Schichtdicken, Orientierungen und Kopf-/Materialprofilen. Verfahrbewegungen tragen kein Material bei. Die dargestellten Materialkörper stammen aus derselben Geometrie wie die Volumenprüfung.

Das nominale Modell beschreibt zusammengeflossene Extrusionsbänder. Die konfigurierte Materialmenge bleibt erhalten; Formorix korrigiert den Flow nicht automatisch, um Lücken oder Überschüsse zu verdecken. Die reale Materialform muss später kalibriert werden.

## Fehlendes Material und Überlagerungen

| Anzeige | Farbe | Bedeutung |
| --- | --- | --- |
| Nominales Material | Blaugrau | Erfolgreich berechnete räumliche Vereinigung aller Extrusionsbänder. |
| Fehlendes Material | Rot | Teil der vorgesehenen 5D-Schale, den diese Hülle nicht abdeckt. |
| Material ausserhalb | Orange | Nominales Material ausserhalb der vorgesehenen 5D-Schale. |
| Überlagerung mit dem Kern | Violett | Nominales 5D-Material liegt im konventionellen Kern. |
| Materialüberlagerung | Violett | Unterschiedlichen Materialien zugeordnete Volumen überschneiden sich. |

Mehrfach belegtes Volumen desselben Materials wird numerisch im Bericht ausgewiesen; dafür existiert kein eigener räumlicher Anzeigekörper. Eine ausgeglichene Gesamtbilanz kann lokale Lücken und Überlagerungen gleichzeitig enthalten und genügt deshalb nicht als Qualitätsnachweis. Geometrisch passende Materialgrenzen bestätigen keine Materialhaftung.

## Alle Lagen

Die Materialprüfung und ihre Volumenanzeigen umfassen immer sämtliche geplanten 5D-Lagen und ausgewählten Bereiche, einschliesslich Bahnenden und Übergängen. Lagenwahl und Zeitschieber filtern diese Materialkörper nicht. Die Materialdiagnose zeigt den berechneten Gesamtzustand, keinen bereits gedruckten Körper. Ihre Anzeigeschalter sind bei fehlenden oder leeren Datensätzen deaktiviert.

## Prüfstatus

Sehr dünne Dreiecke können unterhalb der numerischen Auflösung der GPU-Anzeige liegen und werden dort ausgelassen. Die ursprüngliche Double-Geometrie und ihre Zahlenwerte bleiben unverändert. Eine optisch geschlossene Fläche allein beweist deshalb keine vollständige Materialabdeckung.

**Ungeprüft** oder **unvollständig** bedeutet, dass keine ausreichende Aussage vorliegt, beispielsweise wegen ungültiger Materialkörper, fehlgeschlagener Volumenoperationen oder einer Berechnungsgrenze. Fehlgeschlagene Volumenprüfungen zeigen unbekannte Werte statt scheinbarer Nullfehler. Verfügbare diagnostische Zellen erscheinen nur als Drahtmodell aus gültigen Dreiecken mit endlichen Koordinaten; dieses Modell kann unvollständig sein und ist kein erfolgreich geprüftes Gesamtvolumen. Nicht angezeigte Fehlerkörper dürfen nicht als nachgewiesene Fehlerfreiheit verstanden werden.

Eine Aussage zum Zielwert **0,01 mm** benötigt zusätzlich belastbare Fehlergrenzen für CAD-Referenz beziehungsweise Import, Schalenberechnung, interne Konvertierungen, Bahninterpolation und nominale Materialablage. Ohne bekannte CAD-Referenz ist keine allgemeine CAD-Genauigkeitsbestätigung möglich. Diese geometrische Prüfung verspricht weder reale Druckgenauigkeit noch Tragfähigkeit, Kollisionsfreiheit oder einen ausführbaren Druckauftrag.

Weiter: [Vorschau und Zeitleiste](preview.md) · [Hybrid-Prüfwerte](hybrid.md#was-die-prüfwerte-bedeuten) · [Unterbau und Stützen](supports.md) · [Kopf- und Materialprofile](multi-head.md).
