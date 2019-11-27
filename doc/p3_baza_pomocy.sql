
CREATE TABLE BAZA_POMOCY
(
  appcontext VARCHAR2(255) NOT NULL,
  classname    VARCHAR2(255) NOT NULL,
  windowtitle   VARCHAR2(255) NOT NULL,
  helppage VARCHAR2(255) NOT NULL,
  CONSTRAINT baza_pomocy PRIMARY KEY (appcontext, classname, windowtitle)
);

insert into BAZA_POMOCY values(' ', 'LarFrame', 'Lista ostrzegawcza', '/xwiki/bin/view/Lista%20ostrzegawcza/');
insert into BAZA_POMOCY values(' ', 'LarFrame', 'Lista st', '/xwiki/bin/view/Lista%20stan%C3%B3w/');
insert into BAZA_POMOCY values(' ', 'RListaOperacjiSchematowych', 'Zbiorcza lista', '/xwiki/bin/view/Operacje%20schematowe/');
insert into BAZA_POMOCY values(' ', 'OnlineFrame', ' Lista alarmowa', '/xwiki/bin/view/XWiki/Alarmy/');
insert into BAZA_POMOCY values(' ', 'RListaOperacjiSchematowych', ' Terminarz', '/xwiki/bin/view/Terminarz/');
insert into BAZA_POMOCY values(' ', 'ArchiveFrame', ' Dziennik', '/xwiki/bin/view/Dziennik%20archiwum/');
insert into BAZA_POMOCY values(' ', 'OnlineFrame', ' Dziennik', '/xwiki/bin/view/Dziennik%20zdarze%C5%84/');
insert into BAZA_POMOCY values('mapy', 'RFrame', ' ', '/xwiki/bin/view/XWiki/Obs%C5%82uga%20map/');

/


commit;
