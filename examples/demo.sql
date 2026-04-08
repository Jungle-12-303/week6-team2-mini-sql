-- T-SQL inspired demo script
INSERT INTO users (id, name, age, track) VALUES (1001, 'Alice', 24, 'backend');
INSERT INTO users (id, name, age, track) VALUES (1002, 'Bob', 26, 'database');
SELECT * FROM users;
SELECT name, track FROM users WHERE age = 26;

