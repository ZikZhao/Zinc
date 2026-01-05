type Integer = int;

let a: i8 = 65536;

fn add(a: i32, b: i32) -> i32 {
    return a + b;
}

fn sub(a: i32, b: i32) -> i32 { return a - b; }

fn sub(a: f64, b: f64) -> f64 { return a - b; }

let f: (i32, i32) -> i32 = add;

let g = sub;

fn main() -> i32 {
    f(2, 3);
    g(5, 4);
    g(3.0, 2.0);
}