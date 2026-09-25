-- Overture Maps places (points) inside California's bounding box, exported
-- as a local parquet file every benchmark competitor loads from.
LOAD spatial;
LOAD httpfs;
SET s3_region = 'us-west-2';
.timer on
COPY (
  SELECT
    id,
    names.primary AS name,
    categories.primary AS category,
    ST_X(geometry) AS x,
    ST_Y(geometry) AS y
  FROM read_parquet('s3://overturemaps-us-west-2/release/2026-08-19.0/theme=places/type=place/*', hive_partitioning = 1)
  WHERE bbox.xmin BETWEEN -124.6 AND -114.0
    AND bbox.ymin BETWEEN 32.4 AND 42.1
) TO 'data.noindex/places-ca.parquet' (FORMAT parquet, COMPRESSION zstd);
SELECT count(*) AS rows FROM 'data.noindex/places-ca.parquet';
