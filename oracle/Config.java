
package oracle;

import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.util.ArrayList;
import java.util.List;

public class Config {

    public static class ConfigException extends Exception {
        public ConfigException(String msg) { super(msg); }
    }

    public static int[][] parse (Path file) throws ConfigException, IOException{
        List<String> lines = Files.readAllLines(file) ;
        List<int[]> rows = new ArrayList<>();
        List<Integer> rowLineNumbers = new ArrayList<>();

        for (int n = 0; n < lines.size(); n++){
            String line = lines.get(n).trim();
            if (line.isEmpty() || line.startsWith("#")){
                continue;
            }
        

            String[] parts = line.split("\\s+");
            int[] row = new int[parts.length];
            for (int k = 0 ; k < parts.length ; k++){
                try{
                    row[k] = Integer.parseInt(parts[k]);
                } catch(NumberFormatException e){
                    throw new ConfigException(parts[k] + " is not number");
                }
            }
            rows.add(row);
            rowLineNumbers.add(n+1);
        }

        int N = rows.size() + 1;
        if (N < 2 || N > Protocol.MAX_NODES){
            throw new ConfigException("nodes < 2 or nodes > 26");
        }
        for (int i = 0; i < rows.size();i++){
            int[] row = rows.get(i);
            int lineNo = rowLineNumbers.get(i);
            int expected = N-1-i;

            if (row.length != expected){
                throw new ConfigException("line "+lineNo + ": expected " + expected
                        +" entries, found " + row.length);
            }

            for (int cost : row){
                if (cost != Protocol.NO_LINK && cost <= 0){
                    throw new ConfigException("Each cost should be +ve or -1");
                }
            }
        }

        int[][] cost = new int[N][N];
        for (int i = 0; i < N;i++){
            for (int j = 0 ; j < N;j++){
                cost[i][j] = Protocol.NO_LINK;
            }
        }

        for (int i = 0 ; i < rows.size();i++){
            int[] row = rows.get(i);
            for (int k = 0 ; k < row.length;k++){
                int j = i + 1 + k;
                cost[i][j] = row[k];
                cost[j][i] = row[k];
            }
        }
        return  cost;

    }

    public static void print(int[][] cost){
        int N = cost.length;

  
        StringBuilder header = new StringBuilder("    ");
        for (int j = 0; j < N; j++){
            header.append(String.format("%4s", (char) ('A' + j)));
        }
        System.out.println(header);

        
        for (int i = 0; i < N; i++){
            StringBuilder sb = new StringBuilder();
            sb.append(String.format("%-4s", (char) ('A' + i)));
            for (int j = 0; j < N; j++){
                sb.append(String.format("%4d", cost[i][j]));
            }
            System.out.println(sb);
        }
    }

  

    public static void main(String[] args) throws Exception{
        try {
            System.out.println("Testing..");
            print(parse(Path.of(args[0])));
        } catch (ConfigException e){
            System.err.println("Bad config: " + e.getMessage());
            System.exit(1);
        }
    }

}
