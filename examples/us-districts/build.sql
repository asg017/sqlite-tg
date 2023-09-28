create table districts_2016 as
  select
    name,
    cast(data as text) as data
from zipfile('gh-pages.zip')
where name like 'districts-gh-pages/cds/2016%.geojson';

