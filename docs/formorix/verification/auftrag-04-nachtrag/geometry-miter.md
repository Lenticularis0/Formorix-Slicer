# Gehrungsgrenzen zwischen angrenzenden 5D-Schalen

Prüfstand dieses Teilberichts: Der kombinierte Lauf mit `[FormorixFoundations],[FormorixHybrid],[FormorixPreview],[RibbonModel]`, RNG-Seed 42, besteht mit **57 Testfällen und 221257 Assertions**. Quelle ist das unverändert übernommene [Testprotokoll](geometry-tests-final.txt). Das ist ein geometrischer Teilnachweis; spätere Gesamtregression, Windows-GUI-Prüfung und endgültiger Build-Vergleich werden im Abschlussbericht getrennt dokumentiert.

## Verfahren

Die explizite Policy `MiterAdjacentEdges` erzeugt für genau benachbarte Randkanten einen gemeinsamen inneren Rand. Jeder Bereich behält seine eigene Normale und Dicke. Der innere Punkt `q` erfüllt für den ursprünglichen Randpunkt `p` die lokalen Bedingungen `n_i · (p - q) = d_i`. Die geometrische Sortierung der Bedingungen macht das Ergebnis unabhängig von Regionsnummer und Eingabereihenfolge.

Beide Schalen verwenden identische innere Randpunkte und identische Dreiecksdiagonalen auf den gemeinsamen Randvierecken. Das verhindert unterschiedliche Grenzflächen bei gekrümmten Nähten. Außenpunkte und äußere Dreiecke werden nicht geglättet. CGAL prüft anschließend die gültigen Volumenkörper, disjunkte Innenräume und Volumenerhaltung. Unterschiedliche Materialien bleiben durch eine explizite Regionsgrenzfläche getrennt; daraus folgt keine Materialverträglichkeit oder Haftungsfreigabe.

## Tatsächlich protokollierte Werte

| Geometrie | Volumenresiduum nach Double-Ausgabe, mm³ | Max. CGAL-/Ausgaberundungsgrenze, mm | Max. Normalabstandsresiduum der Gehrung, mm |
| --- | ---: | ---: | ---: |
| Quader: obere Schale 1 mm, vordere Schale 2 mm | 0 | 4.3903019535274468e-10 | 0 |
| Quader: obere Schale 0.6 mm, vordere Schale 1.2 mm | -5.6843418860808015e-14 | 5.6446708000632135e-10 | 3.3306690738754696e-16 |
| Gekrümmte gemeinsame Kante mit wechselnden Bereichsnormalen | 0 | 5.01437e-10 | 3.33067e-16 |
| Originales `curved-wall.3mf`, beide gespeicherten Bereiche | 0 | 6.0011491503645249e-10 | 1.8318679906315083e-14 |

Das Originalprojekt hat im Test ein Eingangsvolumen von **18211.860008239746 mm³**. Seine beiden gespeicherten Schalen haben 0.8 mm beziehungsweise 1.0 mm Dicke; **20 gemeinsame Randkanten** wurden geometrisch gekoppelt. Es entstehen zwei getrennte Schalen und ein Kern mit einer expliziten Grenze zwischen Region 1 und Region 2. Diese geometrische Prüfung wird ohne Düsenorientierungs- oder Druckfreigabe ausgeführt.

Die Werte beschreiben verschiedene Fehlerarten: Ein Volumenresiduum von null ist keine Aussage über CAD-Oberflächenabstände. Das Normalabstandsresiduum misst nur die lokalen Gehrungsbedingungen vor der abschließenden CGAL-Ausgaberundung. Insbesondere ist die Rundungsgrenze kein Gesamtfehler der gekrümmten Schale, der Bahninterpolation oder der nominalen Materialablage. Für die ursprüngliche CAD-Referenz des importierten 3MF liegt hier kein Nachweis vor.

## Umfang der fünf neuen Tests

Die Fälle stehen in [test_formorix_foundations.cpp](../../../../tests/libslic3r/test_formorix_foundations.cpp).

1. Zwei unterschiedliche Dickenpaare am rechtwinkligen Quader: erwartete Teilvolumina, gültige Körper, vollständige Flächeninhalte aller sechs Außenflächen, gemeinsame Materialgrenze und umgekehrte Eingabereihenfolge. Beim Dickenpaar 1/2 mm werden 90/90/320 mm³ für die beiden Schalen und den Kern geprüft.
2. Ein koplanarer Tiefensprung bleibt erhalten. Nicht zusammengehörige Überlappungen und doppelt zugewiesene Oberflächen werden weiter abgewiesen.
3. Ein feines inneres Netz mit erster Reihe bei 0.1 mm und benachbarter Dicke 1 mm wird ausdrücklich als gefalteter Offset abgewiesen. Es bleiben keine akzeptierten Teilkörper; Originalkörper und markierte Oberfläche bleiben unverändert.
4. Eine gekrümmte gemeinsame Kante verwendet wechselnde eigene Normalen. Geprüft werden gemeinsame Grenzfläche, gültige Teilkörper, Volumenerhaltung, unveränderte Eingaben und Reihenfolgeunabhängigkeit.
5. Die beiden echten Markierungen des gespeicherten `curved-wall.3mf` werden gemeinsam partitioniert. Beide Teilkörper und der Kern sowie die gemeinsame Grenze werden überprüft; auch der umgekehrte Aufruf besteht.

## Offene Grenzen

- Randkanten müssen geometrisch exakt zusammenpassen. Es gibt kein heuristisches Verschweißen ähnlicher Kanten.
- Die bestehende innere Triangulierung bleibt erhalten. Eine Gehrung, die eine innere Dreiecksreihe überholt und Zellen umklappt, wird abgelehnt. Eine vollständige tessellierungsunabhängige Boolean-Gehrung beziehungsweise lokale Neu-Triangulierung bleibt offen.
- Inkonsistente Offsetbedingungen und Gehrungen über dem Vierfachen der größten beteiligten Dicke werden abgewiesen. Restliche Volumenüberlappungen werden nicht durch eine Regionspriorität verdeckt.
- Die erfolgreiche Zweiregionen-Geometrie des Originalprojekts ist kein erfolgreicher Zweiregionen-Druckplan. Im selben Lauf weist der Hybridplaner dessen Kombination mit gekippter starrer Düsenquerschnittsform ausdrücklich zurück, weil diese nicht zur verlangten normalen Schichtdicke passt.
- Für gekrümmte Gehrungen, unbekannte CAD-Referenzen, vollständige Materialabdeckung und reale Ablage bleibt die umfassende 0.01-mm-Bestätigung offen. Kollisionen, Haftung und Herstellbarkeit sind hier nicht freigegeben.
