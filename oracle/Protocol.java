package oracle;

public final class Protocol{
    private Protocol(){}

    public static final int TYPE_CONNECT = 1;
    public static final int TYPE_LINK_STATE = 2;

    public static final int HEADER_LEN = 3;

    public static final int CONNECT_BODY_LEN = 6;
    public static final int LS_COUNT_LEN = 1;
    public static final int LS_TUPLE_LEN = 11;
    public static final int MAX_NODES = 26;
    public static final int ON_PORT = 5000;
    public static final int RECV_BUF_SIZE = 2 *(HEADER_LEN + LS_COUNT_LEN + LS_TUPLE_LEN*MAX_NODES);
    public static final int NO_LINK = -1;

    
}