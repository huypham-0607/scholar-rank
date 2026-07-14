# Data Reference

## Data source

Sources are taken from [OpenAlex API](https://developers.openalex.org/)

## All fields for "Works"
| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `id`                      | `string`      | OpenAlex ID for this work.                                        |
| `doi`                     | `string`      | The DOI for the work. This is the Canonical External ID for works.|
| `title`                   | `string/null` | The title of this work.                                           |
| `authorships`              | `author[]`    | The **first 3 author names** of a work.                           |
| `authorship_truncated`    | `bool`        | Whether authorship is truncated.                                  |
| `abstract_inverted_index` | `object/null` | The abstract as an inverted index (word positions).               |
| `type`                    | `string`      | The type of the work. Common values: article, book, dataset.      |
| `language`                | `string/null` | Language in ISO 639-1 format.                                     |
| `primary_location`        | `source`      | A Location object with the primary location of this work.         |
| `publication_year`        | `int/null`    | The year this work was published.                                 |
| `publication_date`        | `string/null` | The day when this work was published (ISO 8601 format).           |
| `referenced_works`        | `string[]`    | OpenAlex IDs for works that this work cites.                      |
| `referenced_works_count`  | `int`         | The number of works that this work cites.                         |
| `cited_by_count`          | `int`         | The number of citations to this work.                             |
| `topics`                  | `topic[]`     | The top ranked Topic for this work, with id, display_name, score. |

## All fields for "author"

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `id`                      | `string`      | OpenAlex ID for this author.                                      |
| `display_name`            | `string`      | Normalized author name.                                           |
| `raw_author_name`         | `string`      | Author name as it appears in work.                                |

## All fields for "source"

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `id`                      | `string`      | OpenAlex ID id for this source.                                   |
| `display_name`            | `string`      | Name of the source.                                               |
| `type`                    | `enum<string>`| Source type (eg. book series, conferences,...).                   |

## All fields for "topic"

| Field name                | Type          | Desc                                                              |
| ------------------------- | ------------- | ------------------------------------------------------------------|
| `id`                      | `string`      | OpenAlex ID id for this topic.                                    |
| `display_name`            | `string`      | Name of the topic.                                                |
| `score`                   | `int`         | OpenAlex score for topic relevance.                               |
| `subfield_id`             | `string`      | OpenAlex ID for subfield.                                      |
| `subfield_display_name`   | `string`      | OpenAlex ID for subfield.                                      |
| `field_id`                | `string`      | OpenAlex ID for field.                                         |
| `field_display_name`      | `string`      | OpenAlex ID for field.                                         |
| `domain_id`               | `string`      | OpenAlex score for domain.                                        |
| `domain_display_name`     | `string`      | OpenAlex score for domain.                                        |