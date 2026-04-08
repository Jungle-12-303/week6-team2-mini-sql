# materials schema

`materials` 테이블은 재료 관리용 예제 테이블입니다.

| column | meaning | example |
| --- | --- | --- |
| `product_name` | 제품명 | `LX2 선바이저` |
| `material_name` | 재료명 | `ASD` |
| `color_name` | 색상명 | `MMH` |
| `product_weight` | 제품중량 | `21G` |

예시 INSERT 값 순서는 아래와 같습니다.

```sql
INSERT INTO materials VALUES ('LX2 선바이저', 'ASD', 'MMH', '21G');
```
