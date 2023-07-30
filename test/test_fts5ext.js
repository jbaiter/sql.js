const LOREM_IPSUM = `
Lorem ipsum dolor sit amet, consetetur sadipscing elitr, sed diam nonumy eirmod
tempor invidunt ut labore et dolore magna aliquyam erat, sed diam voluptua. At
vero eos et accusam et justo duo dolores et ea rebum. Stet clita kasd gubergren,
no sea takimata sanctus est Lorem ipsum dolor sit amet. Lorem ipsum dolor sit
amet, consetetur sadipscing elitr, sed diam nonumy eirmod tempor invidunt ut
labore et dolore magna aliquyam erat, sed diam voluptua. At vero eos et accusam
et justo duo dolores et ea rebum. Stet clita kasd gubergren, no sea takimata
sanctus est Lorem ipsum dolor sit amet.
`
exports.test = function(sql, assert) {
  var db = new sql.Database();
  var res = db.exec("CREATE VIRTUAL TABLE test USING fts5(data);");
  db.run(`INSERT INTO test VALUES ('${LOREM_IPSUM}');`);

  var res = db.exec(`SELECT offsets(test, 0) FROM test WHERE test MATCH '"eirmod tempor"';`);
  var expectedResult = [{
    columns: ['offsets(test, 0)'],
    values: [
      ["0 74 87 0 370 383"]
    ]
  }];
  assert.deepEqual(res, expectedResult, "offsets() function works");
}

if (module == require.main) {
	const target_file = process.argv[2];
  const sql_loader = require('./load_sql_lib');
  sql_loader(target_file).then((sql)=>{
    require('test').run({
      'test fts5 extensions': function(assert){
        exports.test(sql, assert);
      }
    });
  })
  .catch((e)=>{
    console.error(e);
    assert.fail(e);
  });
}
