.load ../../dist/tg0
.timer on

.param set :query '{   "type": "Feature",   "properties": {},   "geometry": {     "type": "Polygon",     "coordinates": [       [         [           -118.27468048882203,           34.056943273618785         ],         [           -118.25037076861341,           34.056943273618785         ],         [           -118.25037076861341,           34.04351544272916         ],         [           -118.27468048882203,           34.04351544272916         ],         [           -118.27468048882203,           34.056943273618785         ]       ]     ]   } }'

with final as (
  select count(*)
  from rtree_buildings, tg_rect_parts(:query) as rect
  where rtree_buildings.minX >= rect.minX and rtree_buildings.maxX <= rect.maxX
    and rtree_buildings.minY >= rect.minY and rtree_buildings.maxY <= rect.maxY
)
select * from final;


-- TODO proper geojson checking after
with final as (
  select count(*)
  from rtree_buildings, tg_rect_parts(:query) as rect
  where rtree_buildings.minX >= rect.minX and rtree_buildings.maxX <= rect.maxX
    and rtree_buildings.minY >= rect.minY and rtree_buildings.maxY <= rect.maxY

)
select * from final;
