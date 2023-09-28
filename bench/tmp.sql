.load ../dist/tg0
.load /usr/local/lib/mod_spatialite.dylib

.mode quote

select GeomFromGeoJSON('{
    "type": "Point",
    "coordinates": [
      -84.9247727,
      38.7301771
    ]
  }');

select AsBinary(
  GeomFromGeoJSON('{
  "type": "Feature",
  "properties": {},
  "geometry": {
    "type": "Point",
    "coordinates": [
      -84.9247727,
      38.7301771
    ]
  }
}')
);
.exit

.param set :geo "cast(readfile('../examples/la-buildings/Building_Footprints.geojson') as text)"
`
select AsBinary(:geo);
.timer on

select length(tg_to_wkb(:geo));


