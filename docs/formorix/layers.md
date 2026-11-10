# 5D-Schichten und Schalendicke

<a id="schichtdicke"></a>
## Schichtdicke

Die 5D-Schichtdicke wird in Millimetern entlang der lokalen Oberflächennormale gemessen. Die erste Vorschau interpoliert flächengewichtete Eckpunktnormalen. Die Lagen werden nach innen versetzt und von innen nach aussen abgefahren. Feinere letzte Lagen verwenden die kleinere eingestellte Dicke an der Aussenhaut.

Bei beispielsweise fünf Lagen mit 0,2 mm liegen die TCP-Lagen bei 0,8 / 0,6 / 0,4 / 0,2 / 0,0 mm Tiefe. Der dargestellte Kern beginnt bei 1,0 mm. Die äusserste TCP-Bahn folgt dem Dreiecksnetz der Solloberfläche. Es wird noch kein Extrusionsquerschnitt simuliert.

<a id="schalendicke"></a>
## Schalendicke

Die Schalendicke ist eine Alternative zur festen Lagenzahl. Bei 0,65 mm Schalendicke und 0,2 mm Lagenabstand entstehen TCP-Lagen bei 0,6 / 0,4 / 0,2 / 0,0 mm. Der Rest bis zur Kerngrenze ist kleiner als eine volle Lage. Die Vorschau berechnet keine Volumen- oder Flusskorrektur für diesen Rest.

**Gesamte Wandstärke** misst die gegenüberliegende Wand entlang der lokalen Normalen. Der erste Algorithmus unterstützt dabei annähernd gleichmässige Wanddicken: maximale Abweichung 0,02 mm oder 10 % der Schichtdicke. Stark veränderliche Wanddicken werden mit einem Hinweis abgelehnt. Eine Tragstruktur ist weiterhin erforderlich; siehe [Supports](supports.md).

**Originalabmessungen beibehalten** bleibt standardmässig aktiv. Dieser Vorschaualgorithmus arbeitet grundsätzlich nach innen, auch wenn die Option deaktiviert wurde. Er erzeugt keine vergrössernde Aussenhaut. Die Option bleibt für spätere Algorithmen gespeichert.

Fehlende Wandstärke, umgeklappte Offsetdreiecke und sich kreuzende projizierte Ränder führen zur Ablehnung des ganzen Bereichs. Der Kern ist eine angenäherte Grenzfläche aus Restnetz, innerem Offset und Randstreifen, kein neues planarisierbares Boolean-Volumen. Im Vollwandmodus wird kein verbleibender Kern dargestellt.
