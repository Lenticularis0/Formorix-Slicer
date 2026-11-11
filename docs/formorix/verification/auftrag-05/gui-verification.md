# Native GUI-Abnahme – Auftrag 05

Geprüft wurde der installierte Windows-Release aus Quellcommit `b066c16391dbee033abbd8a03f7379c6ff9394ac` unter `build/Formorix-Auftrag05`, Sprache `de_DE`, mit dem isolierten Datenverzeichnis `verification/gui-data-05-final`. Dieser Bericht hält die tatsächlich durchgeführten Bedienprüfungen fest. Die genannten Zeitwerte sind die geschätzten **5D-Bahndauern**, keine Rechenlaufzeiten und kein synchronisierter Gesamtauftrag mit Kern, Supports und Wartezeiten.

## Bedienung und Zeitleiste

| Prüfung | Beobachtetes Ergebnis | Bildschirmnachweis |
| --- | --- | --- |
| Drei-Material-Projekt laden und berechnen | Drei unabhängige Kopf-/Materialzuordnungen; 105 s geplanter 5D-Anteil. Beschriftungen 1/2/3 und Arbeits-/Wartezustände erkennbar. | [Zeitleiste](screenshots/01-hybrid-timeline.png) |
| Zeitschritte und Schieberegler | `+1 s`, anschließend `−1 s`: 0 → 1 → 0 s; Schieberegler auf 53 s bewegt. | [Zeitleiste](screenshots/01-hybrid-timeline.png) |
| Wiedergabe | 4× gewählt, bei 53 s gestartet; erste Beobachtung danach bei etwa 54 s, Pause bei 80,6 s. Der pausierte Wert blieb stabil. Zurücksetzen führte auf 0 s. | Bedienbeobachtung und AX-Protokolle `gui-plus-one.txt`, `gui-minus-one.txt`, `gui-scrub.txt`, `gui-playback-pause.txt` |
| Darstellungen umschalten | Nominales 3D-Material, Orca-Kernbahnen, 5D-Bahnen, Unterbau, ungeprüfte Bereiche und diagnostische Zellen bedient. | [Nominales Material](screenshots/02-nominal-material.png), [ungeprüfte Wölbung](screenshots/06-curved-unverified.png) |
| Materialbericht | Deutscher Bericht geöffnet; lange Zeilen mit dem korrigierten Umbruch lesbar. | [Bericht](screenshots/03-material-report.png) |
| Veraltetes Ergebnis | Während einer Wölbungsberechnung Schichtanzahl 4 → 5 geändert. Das alte Ergebnis wurde verworfen, der Änderungsstatus erschien; auch nach Rechenende blieb die Oberfläche im Editor. | AX-Protokoll `gui-invalidate-during-calculation.txt` |

Die letzte Prüfung ist eine Prüfung der Ergebnisinvalidierung. Ein ausdrücklicher nativer Abbruchknopf wurde dabei **nicht** geprüft; Abbruchpfade sind durch automatisierte Tests abgedeckt.

## Speichern, Laden und geometrische Anzeigen

Das Drei-Material-Projekt wurde mit „Speichern unter“ gespeichert, über `Ctrl+O` erneut geöffnet und neu berechnet. Die Projektprüfung bestätigt neun eingebettete native Presets, drei Bereiche, deren Zuordnungen und die unveränderte platzierte Modellgeometrie einschließlich Markierungen. Informative Prozess-Snapshots wurden aus den aktuellen Profilen erneuert. Einzelheiten stehen im [Archivvergleich](gui-project-archive.json) und im [Projekt-Roundtrip-Bericht](project-roundtrip.md).

| Messgröße | Nach erneutem Laden | Negativfall: Kopf 1 ohne explizite lokale Breitenlimits |
| --- | ---: | ---: |
| Zielvolumen aller 5D-Regionen | 39 mm³ | 39 mm³ |
| Materialvereinigung | 39 mm³ | 36,7460467 mm³ |
| Fehlendes Material | 0 mm³ | 2,25395327 mm³ |
| Material außerhalb / Kernüberlagerung | 0 / 0 mm³ | 0 / 0 mm³ |
| Konservative räumliche Abstandsobergrenze | 7,10542736 × 10⁻¹⁵ mm | Unbekannt |
| Beobachtete Abstandsuntergrenze | 0 mm | 0,375658878 mm |
| Geplante 5D-Dauer | 105 s | 101,3 s |

Im positiven Roundtrip betragen die regionalen Volumen 12, 9 und 18 mm³. Das ist ein Nachweis innerhalb des nominalen Material-/Mesh-Modells; die CAD-Referenz bleibt unbekannt. Es erfolgt weder eine pauschale CAD-Genauigkeitsbestätigung von 0,01 mm noch eine physikalische Druckfreigabe. Siehe [neu geladenen Bericht](screenshots/04-reloaded-material-report.png).

Für den Negativfall wurden im Editor bei Kopf 1 die expliziten lokalen Breitenlimits deaktiviert und anschließend neu berechnet. Die Checkbox für fehlendes Material war danach aktiv; **zwei rote Randstreifen** waren sichtbar. Material B blieb bei 9 mm³, die Flowfaktoren von Kopf 1 blieben unverändert bei 0,96 / 1,02 / 0,98. Die unvollständige Abdeckung wurde nicht als genauigkeitsverifiziert ausgegeben. [Räumliche Fehlmenge](screenshots/10-missing-material.png).

## Wölbung und nativer Unterbau

Die gespeicherte Wölbung wurde mit frisch aufgelöster nativer automatischer Linienbreite von 0,45 mm berechnet. Die Anzeige ergab 206,2 s. Das Materialaudit meldete `InvalidSweep` für Lauf 0 / Zelle 331; 36.496 Kontaktproben blieben streng ungeprüft. Die diagnostischen Zellen waren darstellbar. Dieses Ergebnis ist keine gültige räumliche Materialvereinigung und keine positive Genauigkeits- oder Auflagebestätigung.

Die native Support-Gruppe wurde aufgeklappt; die gemeinsam verwendeten Orca-Supportwerte waren sichtbar. Anschließend wurde die lokale Verdichtung ausdrücklich aktiviert, auf 100 % gesetzt und mit wiederhergestellten vier 5D-Schichten neu berechnet. Die RAW-Diagnose zeigte 10.130 nicht belegte Kontaktproben und 7.302 Lücken zur nativen Schichtebene; die gewichtete Fehlfläche änderte sich von 365,087 auf 364,860 mm². Der Filamentflow blieb 0,98. Diese geringe diagnostische Änderung hebt den unbekannten Material-/Auflagestatus nicht auf. [Native Unterbauanzeige](screenshots/07-native-underlay.png).

## Hilfe, Drucksperre und normales Orca-Slicing

Zwei Hilfe-Schaltflächen wurden tatsächlich geöffnet: `material.md#nominales-materialvolumen` und `preview.md`. Der Browser zeigte die passenden GitHub-Seitentitel und Inhalte des veröffentlichten, unveränderlichen Commits `2c182dc885a158886cc8282b3b70164bd658ec48`. Die zusätzliche automatisierte Prüfung aller zentralen Ziele und Anker ist im [Hilfeprotokoll](package9-help-links.json) dokumentiert.

Im experimentellen Hybridprojekt war der G-Code-Export deaktiviert; ein Klick öffnete keinen Speicherdialog. Nach Auswahl des Druckmenüs blieb Drucken deaktiviert; ein Klick öffnete keinen Sendedialog. [Drucksperre](screenshots/05-hybrid-print-blocked.png). Es wurde kein Druckerverkehr versucht; eine Netzwerkaufzeichnung war nicht Bestandteil dieser Prüfung.

Danach wurde `cube-planar.3mf` nativ über `Ctrl+O` geöffnet (Prusa MK4, 0,4-mm-Düse, Generic PLA, 0,2-mm-Schichten), mit Orca gesliced und tatsächlich als `cube-planar-05.gcode` exportiert. Die Erfolgsbenachrichtigung wurde beobachtet; sie war vor Screenshot 09 bereits verschwunden. [Würfelvorschau](screenshots/08-orca-cube.png), [Ansicht nach Export](screenshots/09-orca-export-success.png). Der [G-Code-Vergleich](gui-cube-export.json) bestätigt identische ausführbare Befehle, Bewegungen und Extrusion gegenüber der dokumentierten Würfelbasis; einzelne eingebettete Profil-/Konfigurationsmetadaten unterscheiden sich. Das ist eine Regression für diesen konkreten Auftrag, kein allgemeiner Nachweis für alle Orca-Funktionen.

## Gespeicherte GUI-Projekte

| Artefakt | SHA256 |
| --- | --- |
| [Drei Materialien, Roundtrip](projects/gui-three-materials-final.3mf) | `7cf7b6d958cb2a4d76a4b0c2202a468650285ea56c2d3108ce12f3502461ccbd` |
| [Drei Materialien, feste Breite mit Fehlmenge](projects/gui-three-materials-fixed-width-final.3mf) | `a938b26c61c61e62f741cad77b3b3c9b59793b3de545f065ed5d8fac23967260` |
| [Wölbung mit ausdrücklich gewählter 100-%-Verdichtung](projects/gui-curved-dense-final.3mf) | `10d770e9e0f1ff30b30f9f290e37c9a545899089d6e30e86b70c50e11b52c859` |

Die AX-Rohprotokolle liegen lokal unter `C:\Formorix Slicer\verification\auftrag05\gui-*.txt`; die kompakten Archiv-/Exportprüfungen, Projekte und zehn Screenshots sind hier versioniert.

## Nicht nativ geprüft

Nicht in dieser GUI-Abnahme enthalten sind ein ausdrücklicher Abbruchknopf, nichtleere Überstand-/Überlagerungsanzeigen, reale Druckhardware und andere Betriebssysteme. Für Überstand, Überlagerung und Abbruch existieren automatisierte Tests; sie ersetzen diese ausstehenden sichtbaren Bedienprüfungen nicht. Materialverträglichkeit, Tragfähigkeit, Kollisionsfreiheit, Maschinenkinematik und eine gemeinsame Mehrkopf-Zeitplanung wurden nicht freigegeben.
