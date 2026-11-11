# Drei unabhängige Materialprofile: Prüfumfang

Die Tests in `test_formorix_multi_material.cpp` verwenden einen analytischen Quader von 18 × 4 × 5 mm mit drei unmittelbar angrenzenden, jeweils 6 × 4 mm großen Oberflächenbereichen. Die bekannte CAD-Referenz ist die Ebene z = 5 mm. Jeder Bereich besitzt eine eigene Düse, ein Material und vollständige Prozesswerte.

| Kopf / Material | Düse | nominelle Linienbreite / erlaubte lokale Breite | Lagenhöhe × Anzahl | Temperatur | Filament-/Prozess-/Oberflächenflow | Q-Limit | Geschwindigkeit | Kontakt–TCP |
|---|---|---|---|---|---|---|---|---|
| 1 / A | 0,8 mm | 0,9 mm / 0,8–1,0 mm | 0,25 mm × 2 | 245 °C | 0,96 / 1,02 / 0,98 | 0,6 mm³/s | 75 mm/s | 2 mm |
| 2 / B | 0,4 mm | 0,45 mm / 0,4–0,5 mm | 0,125 mm × 3 | 205 °C | 0,92 / 1,05 / 1,03 | 0,2 mm³/s | 35 mm/s | 5 mm |
| 3 / C | 0,6 mm | 0,7 mm / 0,6–0,8 mm | 0,1875 mm × 4 | 265 °C | 1,04 / 0,97 / 0,94 | 0,45 mm³/s | 55 mm/s | 9 mm |

Der positive Test verlangt in allen sechs Eingabereihenfolgen einen vollständigen räumlichen Materialnachweis über sämtliche Lagen: 12/9/18 mm³ für A/B/C, insgesamt 39 mm³ Schale und 321 mm³ Kern. Fehlendes Material, Material außerhalb der Sollvolumen, doppelte Belegung, Kernintrusion und Überschneidungen unterschiedlicher Materialien müssen null sein. Die beiden gemeinsamen Materialgrenzen werden zusätzlich geometrisch geprüft. Die bekannten Ebenen müssen das nominale 0,01-mm-Fehlerbudget erfüllen; dies ist keine physikalische Druckgenauigkeitsmessung.

Jede reale Extrusion wird gegen ihren Kopf, Materialnamen, Düsenbezug, Temperatur, lokalen Querschnitt, alle drei unveränderten Flowfaktoren, Q-Limit, Geschwindigkeit und TCP-Versatz geprüft. Zellversorgung und zeitabhängige Versorgung müssen getrennt je Kopf übereinstimmen. Die lokale Randfüllung darf die ursprünglichen Profile nicht überschreiben. Die Vorschau nutzt weiterhin einen fortlaufenden Ablauf über die Bereiche; der Test verlangt keine simultane Planung.

Der negative Test entfernt gezielt eine innere B-Lage. Erwartet werden 3 mm³ fehlendes Material ausschließlich in B, obwohl alle drei äußersten Oberflächen weiterhin passend erscheinen. Damit kann die richtige Außengeometrie einen inneren Materialmangel nicht verdecken. Geometrisch passende Grenzen ergeben weiterhin keine Haftungs- oder Unterbaufreigabe.

Die Anforderungen wurden im Windows-Release-Testlauf ausgeführt und bestehen. Die `MULTI_MATERIAL*`-Zeilen im [Paket-4-Protokoll](package4-tests.txt) enthalten die tatsächlichen Messwerte. Das größte zusammengesetzte Fehlerbudget der positiven Referenz beträgt 3,8247032 × 10⁻⁷ mm. Der innere Mangel wird mit 3 mm³ und einer beobachteten globalen Abstandsunterschranke von etwa 0,125 mm erkannt; die vollständige obere Abstandsschranke bleibt bei ausgeschöpftem Budget unbekannt.
