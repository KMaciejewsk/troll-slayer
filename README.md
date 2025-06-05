Zabójca trolli

Zabójca troli to nie zawód, tylko powołanie. Krasnoludy decydujące się na wybór tej ścieżki życiowej są więc dość nietypowe, co oznacza, że trudno w jednym
mieście znieść więcej niż jednego zabójcę. By uniknąć awantur i burd wynikających ze spotkania zabójców, Gotrek - emerytowany, sławny i o wielkim autorytecie - przy pomocy przyjaciela Felixa wprowadzili prosty system mający zapewnić, że naraz w mieście może być tylko jeden zabójca.

Procesy: modelują N zabójców troli. 
Zasób: M miast, każde o innym identyfikatorze
Wymagania: W jednym mieście naraz może się znaleźć tylko jeden zabójca. Zabójcy nie powinni się ubiegać osobno o każde miasto. Przydział miasta powinien wynikać z deterministycznego algorytmu, na podstawie komunikacji między procesami. Nie trzeba zapewnić równego obłożenia miast, jednakże po wizycie w mieście przez pewien czas inni zabójcy nie powinni go odwiedzać. Dopuszczalne jest w szczególności, że kilku zabójców czeka na miasto, mimo że inne są wolne.

Procesy działają z różną prędkością i niektóre mogą wręcz przez jakiś czas nie chcieć odwiedzać miast. Nie powinno to blokować pracy innych procesów.

Projekt na trójkę: Zabójcy mogą się ubiegać osobno o każde miasto, miasta nie muszą odpoczywać po wizycie zabójcy.
